#include "cache.h"
#include "external-odb.h"
#include "odb-helper.h"

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

static int external_odb_config(const char *var, const char *value, void *data)
{
	struct odb_helper *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "odb", &name, &namelen, &subkey) < 0)
		return 0;

	o = find_or_create_helper(name, namelen);

	if (!strcmp(subkey, "scriptcommand"))
		return git_config_string(&o->cmd, var, value);

	return 0;
}

static void external_odb_init(void)
{
	static int initialized;
	struct odb_helper *o;

	if (initialized)
		return;
	initialized = 1;

	git_config(external_odb_config, NULL);

	for (o = helpers; o; o = o->next)
		odb_helper_init(o);
}

const char *external_odb_root(void)
{
	static const char *root;
	if (!root)
		root = git_pathdup("objects/external");
	return root;
}

int external_odb_has_object(const unsigned char *sha1)
{
	struct odb_helper *o;

	external_odb_init();

	for (o = helpers; o; o = o->next) {
		if (!(o->supported_capabilities & ODB_HELPER_CAP_HAVE))
			return 1;
		if (odb_helper_has_object(o, sha1))
			return 1;
	}
	return 0;
}

int external_odb_get_object(const unsigned char *sha1)
{
	struct odb_helper *o;
	const char *path;

	if (!external_odb_has_object(sha1))
		return -1;

	path = sha1_file_name_alt(external_odb_root(), sha1);
	safe_create_leading_directories_const(path);
	prepare_external_alt_odb();

	for (o = helpers; o; o = o->next) {
		struct strbuf tmpfile = STRBUF_INIT;
		int ret;
		int fd;

		if (!odb_helper_has_object(o, sha1))
			continue;

		fd = create_object_tmpfile(&tmpfile, path);
		if (fd < 0) {
			strbuf_release(&tmpfile);
			return -1;
		}

		if (odb_helper_get_object(o, sha1, fd) < 0) {
			close(fd);
			unlink(tmpfile.buf);
			strbuf_release(&tmpfile);
			continue;
		}

		close_sha1_file(fd);
		ret = finalize_object_file(tmpfile.buf, path);
		strbuf_release(&tmpfile);
		if (!ret)
			return 0;
	}

	return -1;
}

int external_odb_put_object(const void *buf, size_t len,
			    const char *type, unsigned char *sha1)
{
	struct odb_helper *o;

	external_odb_init();

	for (o = helpers; o; o = o->next) {
		int r = odb_helper_put_object(o, buf, len, type, sha1);
		if (r <= 0)
			return r;
	}
	return 1;
}
