#include "cache.h"
#include "object.h"
#include "argv-array.h"
#include "odb-helper.h"
#include "run-command.h"
#include "sha1-lookup.h"
#include "sub-process.h"
#include "pkt-line.h"
#include "sigchain.h"

struct read_object_process {
	struct subprocess_entry subprocess;
	unsigned int supported_capabilities;
};

static struct hashmap subprocess_map;

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

static int start_read_object_fn(struct subprocess_entry *subprocess)
{
	int err;
	struct read_object_process *entry = (struct read_object_process *)subprocess;
	struct child_process *process = &subprocess->process;
	char *cap_buf;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = packet_writel(process->in, "git-read-object-client", "version=1", NULL);
	if (err)
		goto done;

	err = strcmp(packet_read_line(process->out, NULL), "git-read-object-server");
	if (err) {
		error("external process '%s' does not support read-object protocol version 1", subprocess->cmd);
		goto done;
	}
	err = strcmp(packet_read_line(process->out, NULL), "version=1");
	if (err)
		goto done;
	err = packet_read_line(process->out, NULL) != NULL;
	if (err)
		goto done;

	err = packet_writel(process->in,
			    "capability=get_git_obj",
			    "capability=get_raw_obj",
			    "capability=get_direct",
			    NULL);
	if (err)
		goto done;

	while ((cap_buf = packet_read_line(process->out, NULL)))
		parse_capabilities(cap_buf, &entry->supported_capabilities, subprocess->cmd);

done:
	sigchain_pop(SIGPIPE);

	return err;
}

static struct read_object_process *launch_read_object_process(const char *cmd)
{
	struct read_object_process *entry = NULL;

	if (!subprocess_map.tablesize)
		hashmap_init(&subprocess_map, (hashmap_cmp_fn) cmd2process_cmp, NULL, 0);
	else
		entry = (struct read_object_process *)subprocess_find_entry(&subprocess_map, cmd);

	fflush(NULL);

	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->supported_capabilities = 0;

		if (subprocess_start(&subprocess_map, &entry->subprocess, cmd, start_read_object_fn)) {
			free(entry);
			return NULL;
		}
	}

	return entry;
}

static int check_object_process_error(int err,
				      const char *status,
				      struct read_object_process *entry,
				      const char *cmd,
				      unsigned int capability)
{
	if (!err)
		return;

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

static int send_init_packets(struct child_process *process,
			     struct strbuf *status)
{
	int err = packet_write_fmt_gently(process->in, "command=init\n") ||
		packet_flush_gently(process->in);

	if (!err) {
		subprocess_read_status(process->out, status);
		err = strcmp(status->buf, "success");
	}
	return err;
}

static int init_object_process(struct odb_helper *o)
{
	int err;
	struct read_object_process *entry;
	struct child_process *process;
	struct strbuf status = STRBUF_INIT;
	const char *cmd = o->cmd;
	uint64_t start;

	start = getnanotime();

	entry = launch_read_object_process(cmd);
	if (!entry)
		return error("Could not launch process for cmd '%s'", cmd);
	process = &entry->subprocess.process;
	o->supported_capabilities = entry->supported_capabilities;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = send_init_packets(process, &status);

	sigchain_pop(SIGPIPE);

	err = check_object_process_error(err, status.buf, entry, cmd, 0);

	trace_performance_since(start, "init_object_process");

	return err;
}

static struct odb_helper_object *odb_helper_lookup(struct odb_helper *o,
						   const unsigned char *sha1);

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
			     struct read_object_process *entry,
			     struct child_process *process,
			     const unsigned char *sha1,
			     int fd,
			     unsigned int *cur_cap,
			     struct strbuf *status)
{
	const char *instruction;
	int err = 0;

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

	subprocess_read_status(process->out, status);

	return strcmp(status->buf, "success");
}

static int read_object_process(struct odb_helper *o, const unsigned char *sha1, int fd)
{
	int err;
	struct read_object_process *entry;
	struct child_process *process;
	struct strbuf status = STRBUF_INIT;
	const char *cmd = o->cmd;
	uint64_t start;
	unsigned int cur_cap = 0;

	start = getnanotime();

	entry = launch_read_object_process(cmd);
	if (!entry)
		return error("Could not launch process for cmd '%s'", cmd);
	process = &entry->subprocess.process;
	o->supported_capabilities = entry->supported_capabilities;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = send_read_packets(o, entry, process, sha1, fd, &cur_cap, &status);

	sigchain_pop(SIGPIPE);

	err = check_object_process_error(err, status.buf, entry, cmd, cur_cap);

	trace_performance_since(start, "read_object_process");

	return err;
}

static int write_object_process(struct odb_helper *o,
				const void *buf, size_t len,
				const char *type, unsigned char *sha1)
{
	int err;
	struct read_object_process *entry;
	struct child_process *process;
	struct strbuf status = STRBUF_INIT;
	const char *cmd = o->cmd;
	uint64_t start;

	start = getnanotime();

	entry = launch_read_object_process(cmd);
	if (!entry)
		return error("Could not launch process for cmd '%s'", cmd);
	process = &entry->subprocess.process;
	o->supported_capabilities = entry->supported_capabilities;

	if (!(ODB_HELPER_CAP_PUT & entry->supported_capabilities))
		return -1;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = packet_write_fmt_gently(process->in, "command=put\n");
	if (err)
		goto done;

	err = packet_write_fmt_gently(process->in, "sha1=%s\n", sha1_to_hex(sha1));
	if (err)
		goto done;

	err = packet_write_fmt_gently(process->in, "size=%"PRIuMAX"\n", len);
	if (err)
		goto done;

	err = packet_write_fmt_gently(process->in, "kind=blob\n");
	if (err)
		goto done;

	err = packet_flush_gently(process->in);
	if (err)
		goto done;

	err = write_packetized_from_buf(buf, len, process->in);
	if (err)
		goto done;

	subprocess_read_status(process->out, &status);
	err = strcmp(status.buf, "success");

done:
	sigchain_pop(SIGPIPE);

	err = check_object_process_error(err, status.buf, entry, cmd, ODB_HELPER_CAP_PUT);

	trace_performance_since(start, "write_object_process");

	return err;
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

int odb_helper_init(struct odb_helper *o)
{
	struct odb_helper_cmd cmd;
	FILE *fh;
	struct strbuf line = STRBUF_INIT;

	if (o->script_mode) {
		if (odb_helper_start(o, &cmd, 0, "init") < 0)
			return -1;

		fh = xfdopen(cmd.child.out, "r");
		while (strbuf_getline(&line, fh) != EOF)
			parse_capabilities(line.buf, &o->supported_capabilities, o->name);

		strbuf_release(&line);
		fclose(fh);
		odb_helper_finish(o, &cmd);
	} else {
		return init_object_process(o);
	}

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

static void odb_helper_load_have(struct odb_helper *o)
{
	struct odb_helper_cmd cmd;
	FILE *fh;
	struct strbuf line = STRBUF_INIT;

	if (o->have_valid)
		return;
	o->have_valid = 1;

	if (odb_helper_start(o, &cmd, 0, "have") < 0)
		return;

	fh = xfdopen(cmd.child.out, "r");
	while (strbuf_getline(&line, fh) != EOF)
		if (add_have_entry(o, line.buf))
			break;

	strbuf_release(&line);
	fclose(fh);
	odb_helper_finish(o, &cmd);

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

static int odb_helper_fetch_plain_object(struct odb_helper *o,
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

static int odb_helper_fetch_git_object(struct odb_helper *o,
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

int odb_helper_fault_in_object(struct odb_helper *o,
			       const unsigned char *sha1)
{
	if (o->supported_capabilities & ODB_HELPER_CAP_HAVE) {
		struct odb_helper_object *obj = odb_helper_lookup(o, sha1);
		if (!obj)
			return -1;
	}

	if (o->script_mode) {
		struct odb_helper_cmd cmd;
		struct odb_helper_object *obj = odb_helper_lookup(o, sha1);

		if (!obj)
			return -1;

		if (odb_helper_start(o, &cmd, 0, "get_direct %s", sha1_to_hex(sha1)) < 0)
			return -1;
		if (odb_helper_finish(o, &cmd))
			return -1;
		return 0;
	} else {
		return read_object_process(o, sha1, -1);
	}
}

int odb_helper_fetch_object(struct odb_helper *o,
			    const unsigned char *sha1,
			    int fd)
{
	if (o->script_mode) {
		if (o->supported_capabilities & ODB_HELPER_CAP_GET_GIT_OBJ)
			return odb_helper_fetch_git_object(o, sha1, fd);
		if (o->supported_capabilities & ODB_HELPER_CAP_GET_RAW_OBJ)
			return odb_helper_fetch_plain_object(o, sha1, fd);
		if (o->supported_capabilities & ODB_HELPER_CAP_GET_DIRECT)
			return 0;

		// TODO maybe use
		//	BUG("invalid fetch kind '%d'", o->fetch_kind);
		return -1;
	} else {
		return read_object_process(o, sha1, fd);
	}
}

int odb_helper_for_each_object(struct odb_helper *o,
			       each_external_object_fn fn,
			       void *data)
{
	int i;
	for (i = 0; i < o->have_nr; i++) {
		struct odb_helper_object *obj = &o->have[i];
		int r = fn(obj->sha1, obj->type, obj->size, data);
		if (r)
			return r;
	}

	return 0;
}

int odb_helper_write_plain_object(struct odb_helper *o,
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
	if (o->script_mode) {
		return odb_helper_write_plain_object(o, buf, len, type, sha1);
	} else {
		return write_object_process(o, buf, len, type, sha1);
	}
}
