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
	const char *key, *dot;

	if (!skip_prefix(var, "odb.", &key))
		return 0;
	dot = strrchr(key, '.');
	if (!dot)
		return 0;

	o = find_or_create_helper(key, dot - key);
	key = dot + 1;

	if (!strcmp(key, "command"))
		return git_config_string(&o->cmd, var, value);

	return 0;
}

static void external_odb_init(void)
{
	static int initialized;

	if (initialized)
		return;
	initialized = 1;

	git_config(external_odb_config, NULL);
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

	for (o = helpers; o; o = o->next)
		if (odb_helper_has_object(o, sha1))
			return 1;
	return 0;
}

int external_odb_fetch_object(const unsigned char *sha1)
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

		if (odb_helper_fetch_object(o, sha1, fd) < 0) {
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

int external_odb_for_each_object(each_external_object_fn fn, void *data)
{
	struct odb_helper *o;

	external_odb_init();

	for (o = helpers; o; o = o->next) {
		int r = odb_helper_for_each_object(o, fn, data);
		if (r)
			return r;
	}
	return 0;
}
