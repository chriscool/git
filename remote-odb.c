#include "cache.h"
#include "remote-odb.h"
#include "odb-helper.h"
#include "config.h"
#include "object-store.h"

static struct odb_helper *helpers;
static struct odb_helper **helpers_tail = &helpers;

static struct odb_helper *find_or_create_helper(const char *name, int len)
{
	struct odb_helper *o;

	for (o = helpers; o; o = o->next)
		if (!strncmp(o->name, name, len) && !o->name[len])
			return o;

	o = odb_helper_new(name, len);
	*helpers_tail = o;
	helpers_tail = &o->next;

	return o;
}

static int remote_odb_config(const char *var, const char *value, void *data)
{
	struct odb_helper *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "odb", &name, &namelen, &subkey) < 0)
		return 0;

	o = find_or_create_helper(name, namelen);

	if (!strcmp(subkey, "promisorremote")) {
		o->type = ODB_HELPER_GIT_REMOTE;
		o->supported_capabilities |= ODB_HELPER_CAP_HAVE;
		o->supported_capabilities |= ODB_HELPER_CAP_GET_DIRECT;
		return git_config_string(&o->remote, var, value);
	}
	if (!strcmp(subkey, "scriptcommand")) {
		o->type = ODB_HELPER_SCRIPT_CMD;
		return git_config_string(&o->command, var, value);
	}
	if (!strcmp(subkey, "subprocesscommand")) {
		o->type = ODB_HELPER_SUBPROCESS_CMD;
		return git_config_string(&o->command, var, value);
	}
	if (!strcmp(subkey, "partialclonefilter"))
		return git_config_string(&o->partial_clone_filter, var, value);

	return 0;
}

static void remote_odb_do_init(int force)
{
	static int initialized;
	struct odb_helper *o;

	if ((!force && initialized) || !use_remote_odb)
		return;
	initialized = 1;

	git_config(remote_odb_config, NULL);

	for (o = helpers; o; o = o->next)
		odb_helper_init(o);
}

static inline void remote_odb_init(void)
{
	remote_odb_do_init(0);
}

inline void remote_odb_reinit(void)
{
	remote_odb_do_init(1);
}

struct odb_helper *find_odb_helper(const char *remote, enum odb_helper_type type)
{
	struct odb_helper *o;

	remote_odb_init();

	if (!remote)
		return helpers;

	for (o = helpers; o; o = o->next)
		if (o->remote && !strcmp(o->remote, remote) &&
		    (o->type == type || o->type == ODB_HELPER_ANY))
			return o;

	return NULL;
}

int has_remote_odb(void)
{
	return !!find_odb_helper(NULL, ODB_HELPER_ANY);
}

const char *remote_odb_root(void)
{
	static const char *root;
	if (!root)
		root = git_pathdup("objects/remote");
	return root;
}

int remote_odb_has_object(const unsigned char *sha1)
{
	struct odb_helper *o;

	remote_odb_init();

	for (o = helpers; o; o = o->next) {
		if (!(o->supported_capabilities & ODB_HELPER_CAP_HAVE))
			return 1;
		if (odb_helper_has_object(o, sha1))
			return 1;
	}
	return 0;
}

int remote_odb_get_object(const unsigned char *sha1)
{
	struct odb_helper *o;
	struct strbuf pathbuf = STRBUF_INIT;

	if (!remote_odb_has_object(sha1))
		return -1;

	sha1_file_name_alt(&pathbuf, remote_odb_root(), sha1);
	safe_create_leading_directories_const(pathbuf.buf);
	prepare_external_alt_odb(the_repository);

	for (o = helpers; o; o = o->next) {
		struct strbuf tmpfile = STRBUF_INIT;
		int ret;
		int fd;

		if (!(o->supported_capabilities & ODB_HELPER_CAP_GET_RAW_OBJ) &&
		    !(o->supported_capabilities & ODB_HELPER_CAP_GET_GIT_OBJ))
			continue;

		fd = create_object_tmpfile(&tmpfile, pathbuf.buf);
		if (fd < 0) {
			strbuf_release(&tmpfile);
			strbuf_release(&pathbuf);
			return -1;
		}

		if (odb_helper_get_object(o, sha1, fd) < 0) {
			close(fd);
			unlink(tmpfile.buf);
			strbuf_release(&tmpfile);
			continue;
		}

		close_sha1_file(fd);
		ret = finalize_object_file(tmpfile.buf, pathbuf.buf);
		strbuf_release(&tmpfile);
		strbuf_release(&pathbuf);
		if (!ret)
			return 0;
	}

	strbuf_release(&pathbuf);

	return -1;
}

int remote_odb_get_direct(const unsigned char *sha1)
{
	struct odb_helper *o;

	trace_printf("trace: remote_odb_get_direct: %s", sha1_to_hex(sha1));

	remote_odb_init();

	for (o = helpers; o; o = o->next) {
		if (!(o->supported_capabilities & ODB_HELPER_CAP_GET_DIRECT))
			continue;
		if (odb_helper_get_direct(o, sha1) < 0)
			continue;
		return 0;
	}

	return -1;
}

int remote_odb_get_many_direct(const struct oid_array *to_get)
{
	struct odb_helper *o;

	trace_printf("trace: remote_odb_get_many_direct: nr: %d", to_get->nr);

	remote_odb_init();

	for (o = helpers; o; o = o->next) {
		if (!(o->supported_capabilities & ODB_HELPER_CAP_GET_DIRECT))
			continue;
		if (odb_helper_get_many_direct(o, to_get) < 0)
			continue;
		return 0;
	}

	return -1;
}

int remote_odb_put_object(const void *buf, size_t len,
			  const char *type, unsigned char *sha1)
{
	struct odb_helper *o;

	remote_odb_init();

	/* For now accept only blobs */
	if (strcmp(type, "blob"))
		return 1;

	for (o = helpers; o; o = o->next) {
		int r = odb_helper_put_object(o, buf, len, type, sha1);
		if (r <= 0)
			return r;
	}
	return 1;
}
