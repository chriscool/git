#include "cache.h"
#include "object.h"
#include "argv-array.h"
#include "odb-helper.h"
#include "run-command.h"
#include "sha1-lookup.h"
#include "sub-process.h"
#include "pkt-line.h"
#include "sigchain.h"

struct object_process {
	struct subprocess_entry subprocess;
	unsigned int supported_capabilities;
};

static struct hashmap subprocess_map;

static int check_object_process_status(int fd, struct strbuf *status)
{
	subprocess_read_status(fd, status);
	return strcmp(status->buf, "success");
}

static void parse_capabilities(char *cap_buf,
			       unsigned int *supported_capabilities,
			       const char *process_name)
{
	struct string_list cap_list = STRING_LIST_INIT_NODUP;

	string_list_split_in_place(&cap_list, cap_buf, '=', 1);

	if (cap_list.nr == 2 && !strcmp(cap_list.items[0].string, "capability")) {
		const char *cap_name = cap_list.items[1].string;

		if (!strcmp(cap_name, "get_git_obj")) {
			*supported_capabilities |= ODB_HELPER_CAP_GET_GIT_OBJ;
		} else if (!strcmp(cap_name, "get_raw_obj")) {
			*supported_capabilities |= ODB_HELPER_CAP_GET_RAW_OBJ;
		} else if (!strcmp(cap_name, "get_direct")) {
			*supported_capabilities |= ODB_HELPER_CAP_GET_DIRECT;
		} else if (!strcmp(cap_name, "put_git_obj")) {
			*supported_capabilities |= ODB_HELPER_CAP_PUT_GIT_OBJ;
		} else if (!strcmp(cap_name, "put_raw_obj")) {
			*supported_capabilities |= ODB_HELPER_CAP_PUT_RAW_OBJ;
		} else if (!strcmp(cap_name, "put_direct")) {
			*supported_capabilities |= ODB_HELPER_CAP_PUT_DIRECT;
		} else if (!strcmp(cap_name, "have")) {
			*supported_capabilities |= ODB_HELPER_CAP_HAVE;
		} else {
			warning("external process '%s' requested unsupported read-object capability '%s'",
				process_name, cap_name);
		}
	}

	string_list_clear(&cap_list, 0);
}

static int send_start_packets(struct child_process *process, const char *cmd)
{
	int err = packet_writel(process->in, "git-read-object-client", "version=1", NULL);
	if (err)
		return err;

	err = strcmp(packet_read_line(process->out, NULL), "git-read-object-server");
	if (err) {
		error("external process '%s' does not support read-object protocol version 1", cmd);
		return err;
	}
	err = strcmp(packet_read_line(process->out, NULL), "version=1");
	if (err)
		return err;
	err = packet_read_line(process->out, NULL) != NULL;
	if (err)
		return err;

	return packet_writel(process->in,
			     "capability=get_git_obj",
			     "capability=get_raw_obj",
			     "capability=get_direct",
			     "capability=put_raw_obj",
			     "capability=have",
			     NULL);
}

static int start_read_object_fn(struct subprocess_entry *subprocess)
{
	int err;
	struct object_process *entry = (struct object_process *)subprocess;
	struct child_process *process = &subprocess->process;
	char *cap_buf;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = send_start_packets(process, subprocess->cmd);

	if (!err)
		while ((cap_buf = packet_read_line(process->out, NULL)))
			parse_capabilities(cap_buf, &entry->supported_capabilities, subprocess->cmd);

	sigchain_pop(SIGPIPE);

	return err;
}

static struct object_process *launch_object_process(struct odb_helper *o,
						    unsigned int capability)
{
	struct object_process *entry = NULL;

	if (!subprocess_map.tablesize)
		hashmap_init(&subprocess_map, (hashmap_cmp_fn) cmd2process_cmp, NULL, 0);
	else
		entry = (struct object_process *)subprocess_find_entry(&subprocess_map, o->cmd);

	fflush(NULL);

	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->supported_capabilities = 0;

		if (subprocess_start(&subprocess_map, &entry->subprocess, o->cmd, start_read_object_fn)) {
			error("Could not launch process for cmd '%s'", o->cmd);
			free(entry);
			return NULL;
		}
	}

	o->supported_capabilities = entry->supported_capabilities;

	if (capability && !(capability & entry->supported_capabilities)) {
		error("The cmd '%s' does not support capability '%d'", o->cmd, capability);
		return NULL;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	return entry;
}

static int check_object_process_error(int err,
				      const char *status,
				      struct object_process *entry,
				      const char *cmd,
				      unsigned int capability)
{
	sigchain_pop(SIGPIPE);

	if (!err)
		return 0;

	if (!strcmp(status, "error")) {
		/* The process signaled a problem with the file. */
	} else if (!strcmp(status, "notfound")) {
		/* Object was not found */
		err = -1;
	} else if (!strcmp(status, "abort")) {
		/*
		 * The process signaled a permanent problem. Don't try to read
		 * objects with the same command for the lifetime of the current
		 * Git process.
		 */
		if (capability)
			entry->supported_capabilities &= ~capability;
	} else {
		/*
		 * Something went wrong with the read-object process.
		 * Force shutdown and restart if needed.
		 */
		error("external object process '%s' failed", cmd);
		subprocess_stop(&subprocess_map, &entry->subprocess);
		free(entry);
	}

	return err;
}

static int send_init_packets(struct object_process *entry,
			     struct strbuf *status)
{
	struct child_process *process = &entry->subprocess.process;

	return packet_write_fmt_gently(process->in, "command=init\n") ||
		packet_flush_gently(process->in) ||
		check_object_process_status(process->out, status);
}

static int init_object_process(struct odb_helper *o)
{
	int err;
	struct strbuf status = STRBUF_INIT;
	struct object_process *entry = launch_object_process(o, 0);
	if (!entry)
		return -1;

	err = send_init_packets(entry, &status);

	return check_object_process_error(err, status.buf, entry,
					  o->cmd, 0);
}

static ssize_t read_packetized_plain_object_to_fd(struct odb_helper *o,
						  const unsigned char *sha1,
						  int fd_in, int fd_out)
{
	ssize_t total_read = 0;
	unsigned long total_got = 0;
	int packet_len;

	char hdr[32];
	int hdrlen;

	int ret = Z_STREAM_END;
	unsigned char compressed[4096];
	git_zstream stream;
	git_SHA_CTX hash;
	unsigned char real_sha1[20];

	off_t size;
	enum object_type type;
	const char *s;
	int pkt_size;
	char *size_buf;

	size_buf = packet_read_line(fd_in, &pkt_size);
	if (!skip_prefix(size_buf, "size=", &s))
		return error("odb helper '%s' did not send size of plain object", o->name);
	size = strtoumax(s, NULL, 10);
	if (!skip_prefix(packet_read_line(fd_in, NULL), "kind=", &s))
		return error("odb helper '%s' did not send kind of plain object", o->name);
	/* Check if the object is not available */
	if (!strcmp(s, "none"))
		return -1;
	type = type_from_string_gently(s, strlen(s), 1);
	if (type < 0)
		return error("odb helper '%s' sent bad type '%s'", o->name, s);

	/* Set it up */
	git_deflate_init(&stream, zlib_compression_level);
	stream.next_out = compressed;
	stream.avail_out = sizeof(compressed);
	git_SHA1_Init(&hash);

	/* First header.. */
	hdrlen = xsnprintf(hdr, sizeof(hdr), "%s %lu", typename(type), size) + 1;
	stream.next_in = (unsigned char *)hdr;
	stream.avail_in = hdrlen;
	while (git_deflate(&stream, 0) == Z_OK)
		; /* nothing */
	git_SHA1_Update(&hash, hdr, hdrlen);

	for (;;) {
		/* packet_read() writes a '\0' extra byte at the end */
		char buf[LARGE_PACKET_DATA_MAX + 1];

		packet_len = packet_read(fd_in, NULL, NULL,
			buf, LARGE_PACKET_DATA_MAX + 1,
			PACKET_READ_GENTLE_ON_EOF);

		if (packet_len <= 0)
			break;

		total_got += packet_len;

		/* Then the data itself.. */
		stream.next_in = (void *)buf;
		stream.avail_in = packet_len;
		do {
			unsigned char *in0 = stream.next_in;
			ret = git_deflate(&stream, Z_FINISH);
			git_SHA1_Update(&hash, in0, stream.next_in - in0);
			write_or_die(fd_out, compressed, stream.next_out - compressed);
			stream.next_out = compressed;
			stream.avail_out = sizeof(compressed);
		} while (ret == Z_OK);

		total_read += packet_len;
	}

	if (packet_len < 0) {
		error("unable to read from odb helper '%s': %s",
		      o->name, strerror(errno));
		git_deflate_end(&stream);
		return packet_len;
	}

	if (ret != Z_STREAM_END) {
		warning("bad zlib data from odb helper '%s' for %s",
			o->name, sha1_to_hex(sha1));
		return -1;
	}

	ret = git_deflate_end_gently(&stream);
	if (ret != Z_OK) {
		warning("deflateEnd on object %s from odb helper '%s' failed (%d)",
			sha1_to_hex(sha1), o->name, ret);
		return -1;
	}
	git_SHA1_Final(real_sha1, &hash);
	if (hashcmp(sha1, real_sha1)) {
		warning("sha1 mismatch from odb helper '%s' for %s (got %s)",
			o->name, sha1_to_hex(sha1), sha1_to_hex(real_sha1));
		return -1;
	}
	if (total_got != size) {
		warning("size mismatch from odb helper '%s' for %s (%lu != %lu)",
			o->name, sha1_to_hex(sha1), total_got, size);
		return -1;
	}

	return total_read;
}

static ssize_t read_packetized_git_object_to_fd(struct odb_helper *o,
						const unsigned char *sha1,
						int fd_in, int fd_out)
{
	ssize_t total_read = 0;
	unsigned long total_got = 0;
	int packet_len;
	git_zstream stream;
	int zret = Z_STREAM_END;
	git_SHA_CTX hash;
	unsigned char real_sha1[20];

	memset(&stream, 0, sizeof(stream));
	git_inflate_init(&stream);
	git_SHA1_Init(&hash);

	for (;;) {
		/* packet_read() writes a '\0' extra byte at the end */
		char buf[LARGE_PACKET_DATA_MAX + 1];

		packet_len = packet_read(fd_in, NULL, NULL,
			buf, LARGE_PACKET_DATA_MAX + 1,
			PACKET_READ_GENTLE_ON_EOF);

		if (packet_len <= 0)
			break;

		write_or_die(fd_out, buf, packet_len);

		stream.next_in = (unsigned char *)buf;
		stream.avail_in = packet_len;
		do {
			unsigned char inflated[4096];
			unsigned long got;

			stream.next_out = inflated;
			stream.avail_out = sizeof(inflated);
			zret = git_inflate(&stream, Z_SYNC_FLUSH);
			got = sizeof(inflated) - stream.avail_out;

			git_SHA1_Update(&hash, inflated, got);
			/* skip header when counting size */
			if (!total_got) {
				const unsigned char *p = memchr(inflated, '\0', got);
				if (p)
					got -= p - inflated + 1;
				else
					got = 0;
			}
			total_got += got;
		} while (stream.avail_in && zret == Z_OK);

		total_read += packet_len;
	}

	git_inflate_end(&stream);

	if (packet_len < 0)
		return packet_len;

	git_SHA1_Final(real_sha1, &hash);

	if (zret != Z_STREAM_END) {
		warning("bad zlib data from odb helper '%s' for %s",
			o->name, sha1_to_hex(sha1));
		return -1;
	}
	if (hashcmp(real_sha1, sha1)) {
		warning("sha1 mismatch from odb helper '%s' for %s (got %s)",
			o->name, sha1_to_hex(sha1), sha1_to_hex(real_sha1));
		return -1;
	}

	return total_read;
}

static int send_read_packets(struct odb_helper *o,
			     struct object_process *entry,
			     const unsigned char *sha1,
			     int fd,
			     unsigned int *cur_cap,
			     struct strbuf *status)
{
	const char *instruction;
	int err;
	struct child_process *process = &entry->subprocess.process;

	if (entry->supported_capabilities & ODB_HELPER_CAP_GET_GIT_OBJ) {
		*cur_cap = ODB_HELPER_CAP_GET_GIT_OBJ;
		instruction = "get_git_obj";
	} else if (entry->supported_capabilities & ODB_HELPER_CAP_GET_RAW_OBJ) {
		*cur_cap = ODB_HELPER_CAP_GET_RAW_OBJ;
		instruction = "get_raw_obj";
	} else if (entry->supported_capabilities & ODB_HELPER_CAP_GET_DIRECT) {
		*cur_cap = ODB_HELPER_CAP_GET_DIRECT;
		instruction = "get_direct";
	} else {
		BUG("No known ODB_HELPER_CAP_GET_XXX capability!");
	}

	err = packet_write_fmt_gently(process->in, "command=%s\n", instruction);
	if (err)
		return err;

	err = packet_write_fmt_gently(process->in, "sha1=%s\n", sha1_to_hex(sha1));
	if (err)
		return err;

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	if (entry->supported_capabilities & ODB_HELPER_CAP_GET_RAW_OBJ)
		err = read_packetized_plain_object_to_fd(o, sha1, process->out, fd) < 0;
	else if (entry->supported_capabilities & ODB_HELPER_CAP_GET_GIT_OBJ)
		err = read_packetized_git_object_to_fd(o, sha1, process->out, fd) < 0;

	return check_object_process_status(process->out, status);
}

static int read_object_process(struct odb_helper *o,
			       const unsigned char *sha1,
			       int fd)
{
	int err;
	struct strbuf status = STRBUF_INIT;
	unsigned int cur_cap = 0;
	struct object_process *entry = launch_object_process(o, 0);
	if (!entry)
		return -1;

	err = send_read_packets(o, entry, sha1, fd, &cur_cap, &status);

	return check_object_process_error(err, status.buf, entry,
					  o->cmd, cur_cap);
}

static int send_write_packets(struct object_process *entry,
			      const unsigned char *sha1,
			      const void *buf,
			      size_t len,
			      struct strbuf *status)
{
	struct child_process *process = &entry->subprocess.process;
	int err = packet_write_fmt_gently(process->in, "command=put_raw_obj\n");
	if (err)
		return err;

	err = packet_write_fmt_gently(process->in, "sha1=%s\n", sha1_to_hex(sha1));
	if (err)
		return err;

	err = packet_write_fmt_gently(process->in, "size=%"PRIuMAX"\n", len);
	if (err)
		return err;

	err = packet_write_fmt_gently(process->in, "kind=blob\n");
	if (err)
		return err;

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	err = write_packetized_from_buf(buf, len, process->in);
	if (err)
		return err;

	return check_object_process_status(process->out, status);
}

static int write_object_process(struct odb_helper *o,
				const void *buf, size_t len,
				const char *type, unsigned char *sha1)
{
	int err;
	struct object_process *entry;
	struct strbuf status = STRBUF_INIT;

	entry = launch_object_process(o, ODB_HELPER_CAP_PUT_RAW_OBJ);
	if (!entry)
		return -1;

	err = send_write_packets(entry, sha1, buf, len, &status);

	return check_object_process_error(err, status.buf, entry, o->cmd,
					  ODB_HELPER_CAP_PUT_RAW_OBJ);
}

struct odb_helper *odb_helper_new(const char *name, int namelen)
{
	struct odb_helper *o;

	o = xcalloc(1, sizeof(*o));
	o->name = xmemdupz(name, namelen);

	return o;
}

struct odb_helper_cmd {
	struct argv_array argv;
	struct child_process child;
};

/*
 * Callers are responsible to ensure that the result of vaddf(fmt, ap)
 * is properly shell-quoted.
 */
static void prepare_helper_command(struct argv_array *argv, const char *cmd,
				   const char *fmt, va_list ap)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_addstr(&buf, cmd);
	strbuf_addch(&buf, ' ');
	strbuf_vaddf(&buf, fmt, ap);

	argv_array_push(argv, buf.buf);
	strbuf_release(&buf);
}

__attribute__((format (printf,4,5)))
static int odb_helper_start(struct odb_helper *o,
			    struct odb_helper_cmd *cmd,
			    int use_stdin,
			    const char *fmt, ...)
{
	va_list ap;

	memset(cmd, 0, sizeof(*cmd));
	argv_array_init(&cmd->argv);

	if (!o->cmd)
		return -1;

	va_start(ap, fmt);
	prepare_helper_command(&cmd->argv, o->cmd, fmt, ap);
	va_end(ap);

	cmd->child.argv = cmd->argv.argv;
	cmd->child.use_shell = 1;
	if (use_stdin)
		cmd->child.in = -1;
	else
		cmd->child.no_stdin = 1;
	cmd->child.out = -1;

	if (start_command(&cmd->child) < 0) {
		argv_array_clear(&cmd->argv);
		return -1;
	}

	return 0;
}

static int odb_helper_finish(struct odb_helper *o,
			     struct odb_helper_cmd *cmd)
{
	int ret = finish_command(&cmd->child);
	argv_array_clear(&cmd->argv);
	if (ret) {
		warning("odb helper '%s' reported failure", o->name);
		return -1;
	}
	return 0;
}

static int init_object_script(struct odb_helper *o)
{
	struct odb_helper_cmd cmd;
	FILE *fh;
	struct strbuf line = STRBUF_INIT;

	if (odb_helper_start(o, &cmd, 0, "init") < 0)
		return -1;

	fh = xfdopen(cmd.child.out, "r");
	while (strbuf_getline(&line, fh) != EOF)
		parse_capabilities(line.buf, &o->supported_capabilities, o->name);

	strbuf_release(&line);
	fclose(fh);
	odb_helper_finish(o, &cmd);

	return 0;
}

int odb_helper_init(struct odb_helper *o)
{
	int res;
	uint64_t start = getnanotime();

	if (o->script_mode)
		res = init_object_script(o);
	else
		res = init_object_process(o);

	trace_performance_since(start, "odb_helper_init");

	return 0;
}

static int parse_object_line(struct odb_helper_object *o, const char *line)
{
	char *end;
	if (get_sha1_hex(line, o->sha1) < 0)
		return -1;

	line += 40;
	if (*line++ != ' ')
		return -1;

	o->size = strtoul(line, &end, 10);
	if (line == end || *end++ != ' ')
		return -1;

	o->type = type_from_string(end);
	return 0;
}

static int add_have_entry(struct odb_helper *o, const char *line)
{
	ALLOC_GROW(o->have, o->have_nr+1, o->have_alloc);
	if (parse_object_line(&o->have[o->have_nr], line) < 0) {
		warning("bad 'have' input from odb helper '%s': %s",
			o->name, line);
		return 1;
	}
	o->have_nr++;
	return 0;
}

static int odb_helper_object_cmp(const void *va, const void *vb)
{
	const struct odb_helper_object *a = va, *b = vb;
	return hashcmp(a->sha1, b->sha1);
}

static void have_object_script(struct odb_helper *o)
{
	struct odb_helper_cmd cmd;
	FILE *fh;
	struct strbuf line = STRBUF_INIT;

	if (odb_helper_start(o, &cmd, 0, "have") < 0)
		return;

	fh = xfdopen(cmd.child.out, "r");
	while (strbuf_getline(&line, fh) != EOF)
		if (add_have_entry(o, line.buf))
			break;

	strbuf_release(&line);
	fclose(fh);
	odb_helper_finish(o, &cmd);
}

static void odb_helper_load_have(struct odb_helper *o)
{
	if (o->have_valid)
		return;
	o->have_valid = 1;

	if (o->script_mode)
		have_object_script(o);

	qsort(o->have, o->have_nr, sizeof(*o->have), odb_helper_object_cmp);
}

static struct odb_helper_object *odb_helper_lookup(struct odb_helper *o,
						   const unsigned char *sha1)
{
	int idx;

	odb_helper_load_have(o);
	idx = sha1_entry_pos(o->have, sizeof(*o->have), 0,
			     0, o->have_nr, o->have_nr,
			     sha1);
	if (idx < 0)
		return NULL;
	return &o->have[idx];
}

int odb_helper_has_object(struct odb_helper *o, const unsigned char *sha1)
{
	return !!odb_helper_lookup(o, sha1);
}

static int odb_helper_get_plain_object(struct odb_helper *o,
				       const unsigned char *sha1,
				       int fd)
{
	struct odb_helper_object *obj;
	struct odb_helper_cmd cmd;
	unsigned long total_got = 0;

	char hdr[32];
	int hdrlen;

	int ret = Z_STREAM_END;
	unsigned char compressed[4096];
	git_zstream stream;
	git_SHA_CTX hash;
	unsigned char real_sha1[20];

	obj = odb_helper_lookup(o, sha1);
	if (!obj)
		return -1;

	if (odb_helper_start(o, &cmd, 0, "get_raw_obj %s", sha1_to_hex(sha1)) < 0)
		return -1;

	/* Set it up */
	git_deflate_init(&stream, zlib_compression_level);
	stream.next_out = compressed;
	stream.avail_out = sizeof(compressed);
	git_SHA1_Init(&hash);

	/* First header.. */
	hdrlen = xsnprintf(hdr, sizeof(hdr), "%s %lu", typename(obj->type), obj->size) + 1;
	stream.next_in = (unsigned char *)hdr;
	stream.avail_in = hdrlen;
	while (git_deflate(&stream, 0) == Z_OK)
		; /* nothing */
	git_SHA1_Update(&hash, hdr, hdrlen);

	for (;;) {
		unsigned char buf[4096];
		int r;

		r = xread(cmd.child.out, buf, sizeof(buf));
		if (r < 0) {
			error("unable to read from odb helper '%s': %s",
			      o->name, strerror(errno));
			close(cmd.child.out);
			odb_helper_finish(o, &cmd);
			git_deflate_end(&stream);
			return -1;
		}
		if (r == 0)
			break;

		total_got += r;

		/* Then the data itself.. */
		stream.next_in = (void *)buf;
		stream.avail_in = r;
		do {
			unsigned char *in0 = stream.next_in;
			ret = git_deflate(&stream, Z_FINISH);
			git_SHA1_Update(&hash, in0, stream.next_in - in0);
			write_or_die(fd, compressed, stream.next_out - compressed);
			stream.next_out = compressed;
			stream.avail_out = sizeof(compressed);
		} while (ret == Z_OK);
	}

	close(cmd.child.out);
	if (ret != Z_STREAM_END) {
		warning("bad zlib data from odb helper '%s' for %s",
			o->name, sha1_to_hex(sha1));
		return -1;
	}
	ret = git_deflate_end_gently(&stream);
	if (ret != Z_OK) {
		warning("deflateEnd on object %s from odb helper '%s' failed (%d)",
			sha1_to_hex(sha1), o->name, ret);
		return -1;
	}
	git_SHA1_Final(real_sha1, &hash);
	if (hashcmp(sha1, real_sha1)) {
		warning("sha1 mismatch from odb helper '%s' for %s (got %s)",
			o->name, sha1_to_hex(sha1), sha1_to_hex(real_sha1));
		return -1;
	}
	if (odb_helper_finish(o, &cmd))
		return -1;
	if (total_got != obj->size) {
		warning("size mismatch from odb helper '%s' for %s (%lu != %lu)",
			o->name, sha1_to_hex(sha1), total_got, obj->size);
		return -1;
	}

	return 0;
}

static int odb_helper_get_git_object(struct odb_helper *o,
				     const unsigned char *sha1,
				     int fd)
{
	struct odb_helper_object *obj;
	struct odb_helper_cmd cmd;
	unsigned long total_got;
	git_zstream stream;
	int zret = Z_STREAM_END;
	git_SHA_CTX hash;
	unsigned char real_sha1[20];

	obj = odb_helper_lookup(o, sha1);
	if (!obj)
		return -1;

	if (odb_helper_start(o, &cmd, 0, "get_git_obj %s", sha1_to_hex(sha1)) < 0)
		return -1;

	memset(&stream, 0, sizeof(stream));
	git_inflate_init(&stream);
	git_SHA1_Init(&hash);
	total_got = 0;

	for (;;) {
		unsigned char buf[4096];
		int r;

		r = xread(cmd.child.out, buf, sizeof(buf));
		if (r < 0) {
			error("unable to read from odb helper '%s': %s",
			      o->name, strerror(errno));
			close(cmd.child.out);
			odb_helper_finish(o, &cmd);
			git_inflate_end(&stream);
			return -1;
		}
		if (r == 0)
			break;

		write_or_die(fd, buf, r);

		stream.next_in = buf;
		stream.avail_in = r;
		do {
			unsigned char inflated[4096];
			unsigned long got;

			stream.next_out = inflated;
			stream.avail_out = sizeof(inflated);
			zret = git_inflate(&stream, Z_SYNC_FLUSH);
			got = sizeof(inflated) - stream.avail_out;

			git_SHA1_Update(&hash, inflated, got);
			/* skip header when counting size */
			if (!total_got) {
				const unsigned char *p = memchr(inflated, '\0', got);
				if (p)
					got -= p - inflated + 1;
				else
					got = 0;
			}
			total_got += got;
		} while (stream.avail_in && zret == Z_OK);
	}

	close(cmd.child.out);
	git_inflate_end(&stream);
	git_SHA1_Final(real_sha1, &hash);
	if (odb_helper_finish(o, &cmd))
		return -1;
	if (zret != Z_STREAM_END) {
		warning("bad zlib data from odb helper '%s' for %s",
			o->name, sha1_to_hex(sha1));
		return -1;
	}
	if (total_got != obj->size) {
		warning("size mismatch from odb helper '%s' for %s (%lu != %lu)",
			o->name, sha1_to_hex(sha1), total_got, obj->size);
		return -1;
	}
	if (hashcmp(real_sha1, sha1)) {
		warning("sha1 mismatch from odb helper '%s' for %s (got %s)",
			o->name, sha1_to_hex(sha1), sha1_to_hex(real_sha1));
		return -1;
	}

	return 0;
}

static int fault_in_object_script(struct odb_helper *o, const unsigned char *sha1)
{
	struct odb_helper_cmd cmd;

	if (odb_helper_start(o, &cmd, 0, "get_direct %s", sha1_to_hex(sha1)) < 0)
		return -1;
	if (odb_helper_finish(o, &cmd))
		return -1;
	return 0;
}

int odb_helper_fault_in_object(struct odb_helper *o,
			       const unsigned char *sha1)
{
	int res;
	uint64_t start;

	if (o->supported_capabilities & ODB_HELPER_CAP_HAVE) {
		struct odb_helper_object *obj = odb_helper_lookup(o, sha1);
		if (!obj)
			return -1;
	}

	start = getnanotime();

	if (o->script_mode)
		res = fault_in_object_script(o, sha1);
	else
		res = read_object_process(o, sha1, -1);

	trace_performance_since(start, "odb_helper_fault_in_object");

	return res;
}

static int read_object_script(struct odb_helper *o, const unsigned char *sha1, int fd)
{
	if (o->supported_capabilities & ODB_HELPER_CAP_GET_GIT_OBJ)
		return odb_helper_get_git_object(o, sha1, fd);
	if (o->supported_capabilities & ODB_HELPER_CAP_GET_RAW_OBJ)
		return odb_helper_get_plain_object(o, sha1, fd);
	if (o->supported_capabilities & ODB_HELPER_CAP_GET_DIRECT)
		return 0;

	// TODO maybe use
	//	BUG("invalid get kind '%d'", o->fetch_kind);
	return -1;
}

int odb_helper_get_object(struct odb_helper *o,
			  const unsigned char *sha1,
			  int fd)
{
	int res;
	uint64_t start = getnanotime();

	if (o->script_mode)
		res = read_object_script(o, sha1, fd);
	else
		res = read_object_process(o, sha1, fd);

	trace_performance_since(start, "odb_helper_get_object");

	return res;
}

static int write_plain_object_script(struct odb_helper *o,
				     const void *buf, size_t len,
				     const char *type, unsigned char *sha1)
{
	struct odb_helper_cmd cmd;

	if (odb_helper_start(o, &cmd, 1, "put_raw_obj %s %"PRIuMAX" %s",
			     sha1_to_hex(sha1), (uintmax_t)len, type) < 0)
		return -1;

	do {
		int w = xwrite(cmd.child.in, buf, len);
		if (w < 0) {
			error("unable to write to odb helper '%s': %s",
			      o->name, strerror(errno));
			close(cmd.child.in);
			close(cmd.child.out);
			odb_helper_finish(o, &cmd);
			return -1;
		}
		len -= w;
	} while (len > 0);

	close(cmd.child.in);
	close(cmd.child.out);
	odb_helper_finish(o, &cmd);
	return 0;
}

int odb_helper_write_object(struct odb_helper *o,
			    const void *buf, size_t len,
			    const char *type, unsigned char *sha1)
{
	int res;
	uint64_t start = getnanotime();

	if (o->script_mode)
		res = write_plain_object_script(o, buf, len, type, sha1);
	else
		res = write_object_process(o, buf, len, type, sha1);

	trace_performance_since(start, "odb_helper_write_object");

	return res;
}
