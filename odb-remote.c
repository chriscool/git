#include "cache.h"
#include "odb-remote.h"
#include "odb-helper.h"
#include "config.h"

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

static int odb_remote_config(const char *var, const char *value, void *data)
{
	struct odb_helper *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "odb", &name, &namelen, &subkey) < 0)
		return 0;

	o = find_or_create_helper(name, namelen);

	if (!strcmp(subkey, "promisorremote"))
		return git_config_string(&o->dealer, var, value);

	return 0;
}

static void odb_remote_do_init(int force)
{
	static int initialized;

	if (!force && initialized)
		return;
	initialized = 1;

	git_config(odb_remote_config, NULL);
}

static inline void odb_remote_init(void)
{
	odb_remote_do_init(0);
}

inline void odb_remote_reinit(void)
{
	odb_remote_do_init(1);
}

struct odb_helper *find_odb_helper(const char *dealer)
{
	struct odb_helper *o;

	odb_remote_init();

	if (!dealer)
		return helpers;

	for (o = helpers; o; o = o->next)
		if (!strcmp(o->dealer, dealer))
			return o;

	return NULL;
}

int has_odb_remote(void)
{
	return !!find_odb_helper(NULL);
}

const char *odb_remote_root(void)
{
	static const char *root;
	if (!root)
		root = git_pathdup("objects/external");
	return root;
}

int odb_remote_get_direct(const unsigned char *sha1)
{
	struct odb_helper *o;

	odb_remote_init();

	for (o = helpers; o; o = o->next) {
		if (odb_helper_get_direct(o, sha1) < 0)
			continue;
		return 0;
	}

	return -1;
}

int odb_remote_get_many_direct(const struct oid_array *to_get)
{
	struct odb_helper *o;

	odb_remote_init();

	for (o = helpers; o; o = o->next) {
		if (odb_helper_get_many_direct(o, to_get) < 0)
			continue;
		return 0;
	}

	return -1;
}
