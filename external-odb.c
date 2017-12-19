#include "cache.h"
#include "external-odb.h"
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

static int external_odb_config(const char *var, const char *value, void *data)
{
	struct odb_helper *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "odb", &name, &namelen, &subkey) < 0)
		return 0;

	o = find_or_create_helper(name, namelen);

	if (!strcmp(subkey, "promisorremote"))
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

int has_external_odb(void)
{
	external_odb_init();

	return !!helpers;
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

	if (!use_external_odb)
		return 0;

	external_odb_init();

	for (o = helpers; o; o = o->next)
		if (odb_helper_has_object(o, sha1))
			return 1;
	return 0;
}

int external_odb_get_direct(const unsigned char *sha1)
{
	struct odb_helper *o;

	for (o = helpers; o; o = o->next) {
		if (odb_helper_get_direct(o, sha1, 0) < 0)
			continue;
		return 0;
	}

	return -1;
}
