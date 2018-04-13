#include "cache.h"
#include "odb-remote.h"
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

static int external_odb_config(const char *var, const char *value, void *data)
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
		return git_config_string(&o->dealer, var, value);
	}
	if (!strcmp(subkey, "scriptcommand")) {
		o->type = ODB_HELPER_SCRIPT_CMD;
		return git_config_string(&o->dealer, var, value);
	}

	return 0;
}

static void external_odb_do_init(int force)
{
	static int initialized;

	if ((!force && initialized) || !use_external_odb)
		return;
	initialized = 1;

	git_config(external_odb_config, NULL);
}

static inline void external_odb_init(void)
{
	external_odb_do_init(0);
}

inline void external_odb_reinit(void)
{
	external_odb_do_init(1);
}

int has_external_odb(void)
{
	external_odb_init();

	return !!helpers;
}

struct odb_helper *find_odb_helper(const char *dealer, enum odb_helper_type type)
{
	struct odb_helper *o;

	external_odb_init();

	for (o = helpers; o; o = o->next)
		if (!strcmp(o->dealer, dealer) && o->type == type)
			return o;

	return NULL;
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

int external_odb_get_object(const unsigned char *sha1)
{
	struct odb_helper *o;
	struct strbuf pathbuf = STRBUF_INIT;

	if (!external_odb_has_object(sha1))
		return -1;

	sha1_file_name_alt(&pathbuf, external_odb_root(), sha1);
	safe_create_leading_directories_const(pathbuf.buf);
	prepare_external_alt_odb(the_repository);

	for (o = helpers; o; o = o->next) {
		struct strbuf tmpfile = STRBUF_INIT;
		int ret;
		int fd;

		if (!odb_helper_has_object(o, sha1))
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

int external_odb_get_direct(const unsigned char *sha1)
{
	struct odb_helper *o;

	external_odb_init();

	for (o = helpers; o; o = o->next) {
		if (odb_helper_get_direct(o, sha1) < 0)
			continue;
		return 0;
	}

	return -1;
}
