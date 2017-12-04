#include "cache.h"
#include "remote-odb.h"
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

static int remote_odb_config(const char *var, const char *value, void *data)
{
	struct odb_helper *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "odb", &name, &namelen, &subkey) < 0)
		return 0;

	o = find_or_create_helper(name, namelen);

	if (!strcmp(subkey, "promisorremote"))
		return git_config_string(&o->remote, var, value);

	return 0;
}

static void remote_odb_init(void)
{
	static int initialized;

	if (initialized)
		return;
	initialized = 1;

	git_config(remote_odb_config, NULL);
}

struct odb_helper *find_odb_helper(const char *remote)
{
	struct odb_helper *o;

	remote_odb_init();

	if (!remote)
		return helpers;

	for (o = helpers; o; o = o->next)
		if (!strcmp(o->remote, remote))
			return o;

	return NULL;
}

int has_remote_odb(void)
{
	return !!find_odb_helper(NULL);
}
