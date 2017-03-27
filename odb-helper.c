#include "cache.h"
#include "object.h"
#include "argv-array.h"
#include "odb-helper.h"
#include "run-command.h"
#include "sha1-lookup.h"
#include "fetch-object.h"

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

__attribute__((format (printf,3,4)))
static int odb_helper_start(struct odb_helper *o,
			    struct odb_helper_cmd *cmd,
			    const char *fmt, ...)
{
	va_list ap;

	memset(cmd, 0, sizeof(*cmd));
	argv_array_init(&cmd->argv);

	if (!o->command)
		return -1;

	va_start(ap, fmt);
	prepare_helper_command(&cmd->argv, o->command, fmt, ap);
	va_end(ap);

	cmd->child.argv = cmd->argv.argv;
	cmd->child.use_shell = 1;
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

	if (o->initialized)
		return 0;
	o->initialized = 1;

	if (odb_helper_start(o, &cmd, "init") < 0)
		return -1;

	fh = xfdopen(cmd.child.out, "r");
	while (strbuf_getline(&line, fh) != EOF)
		parse_capabilities(line.buf, &o->supported_capabilities, o->name);

	strbuf_release(&line);
	fclose(fh);
	odb_helper_finish(o, &cmd);

	return 0;
}

static int parse_object_line(struct odb_helper_object *o, const char *line)
{
	char *end;
	if (get_oid_hex(line, &o->oid) < 0)
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
	return oidcmp(&a->oid, &b->oid);
}

static void have_object_script(struct odb_helper *o)
{
	struct odb_helper_cmd cmd;
	FILE *fh;
	struct strbuf line = STRBUF_INIT;

	if (odb_helper_start(o, &cmd, "have") < 0)
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

	if (o->type == ODB_HELPER_SCRIPT_CMD)
		have_object_script(o);

	qsort(o->have, o->have_nr, sizeof(*o->have), odb_helper_object_cmp);
}

static const unsigned char *have_sha1_access(size_t index, void *table)
{
	struct odb_helper_object *have = table;
	return have[index].oid.hash;
}

static struct odb_helper_object *odb_helper_lookup(struct odb_helper *o,
						   const unsigned char *sha1)
{
	int idx;

	odb_helper_load_have(o);
	idx = sha1_pos(sha1, o->have, o->have_nr, have_sha1_access);
	if (idx < 0)
		return NULL;
	return &o->have[idx];
}

int odb_helper_has_object(struct odb_helper *o, const unsigned char *sha1)
{
	return !!odb_helper_lookup(o, sha1);
}

int odb_helper_get_object(struct odb_helper *o, const unsigned char *sha1,
			    int fd)
{
	struct odb_helper_object *obj;
	struct odb_helper_cmd cmd;
	unsigned long total_got;
	git_zstream stream;
	int zret = Z_STREAM_END;
	git_SHA_CTX hash;
	unsigned char real_sha1[20];
	struct strbuf header = STRBUF_INIT;
	unsigned long hdr_size;

	obj = odb_helper_lookup(o, sha1);
	if (!obj)
		return -1;

	if (odb_helper_start(o, &cmd, "get_git_obj %s", sha1_to_hex(sha1)) < 0)
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
				if (p) {
					unsigned long hdr_last = p - inflated + 1;
					strbuf_add(&header, inflated, hdr_last);
					got -= hdr_last;
				} else {
					strbuf_add(&header, inflated, got);
					got = 0;
				}
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
	if (parse_sha1_header(header.buf, &hdr_size) < 0) {
		warning("could not parse header from odb helper '%s' for %s",
			o->name, sha1_to_hex(sha1));
		return -1;
	}
	if (total_got != hdr_size) {
		warning("size mismatch from odb helper '%s' for %s (%lu != %lu)",
			o->name, sha1_to_hex(sha1), total_got, hdr_size);
		return -1;
	}

	return 0;
}

int odb_helper_get_direct(struct odb_helper *o,
			  const unsigned char *sha1)
{
	int res = 0;
	uint64_t start = getnanotime();

	if (o->type == ODB_HELPER_GIT_REMOTE)
		res = fetch_object(o->remote, sha1);

	trace_performance_since(start, "odb_helper_get_direct");

	return res;
}

int odb_helper_get_many_direct(struct odb_helper *o,
			       const struct oid_array *to_get)
{
	int res = 0;
	uint64_t start;

	start = getnanotime();

	if (o->type == ODB_HELPER_GIT_REMOTE)
		res = fetch_objects(o->remote, to_get);

	trace_performance_since(start, "odb_helper_get_many_direct");

	return res;
}
