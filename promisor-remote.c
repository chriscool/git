#include "cache.h"
#include "promisor-remote.h"
#include "config.h"

static struct promisor_remote *promisors;
static struct promisor_remote **promisors_tail = &promisors;

struct promisor_remote *promisor_remote_new(const char *name, int namelen)
{
	struct promisor_remote *o;

	o = xcalloc(1, sizeof(*o));
	o->remote_name = xmemdupz(name, namelen);

	return o;
}

static struct promisor_remote *do_find_promisor_remote(const char *remote_name)
{
	struct promisor_remote *o;

	for (o = promisors; o; o = o->next)
		if (o->remote_name && !strcmp(o->remote_name, remote_name))
			return o;

	return NULL;
}

static int promisor_remote_config(const char *var, const char *value, void *data)
{
	struct promisor_remote *o;
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "remote", &name, &namelen, &subkey) < 0)
		return 0;

	if (!strcmp(subkey, "promisor")) {
		if (!git_config_bool(var, value))
			return 0;

		if (do_find_promisor_remote(name))
			return error(_("when parsing config key '%s' "
				       "promisor remote '%s' already exists"),
				     var, name);

		o = promisor_remote_new(name, namelen);
		*promisors_tail = o;
		promisors_tail = &o->next;

		return 0;
	}

	return 0;
}

static void promisor_remote_init(void)
{
	static int initialized;

	if (initialized)
		return;
	initialized = 1;

	git_config(promisor_remote_config, NULL);
}

struct promisor_remote *find_promisor_remote(const char *remote_name)
{
	promisor_remote_init();

	if (!remote_name)
		return promisors;

	return do_find_promisor_remote(remote_name);
}

int has_promisor_remote(void)
{
	return !!find_promisor_remote(NULL);
}
