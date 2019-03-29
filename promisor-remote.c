#include "cache.h"
#include "promisor-remote.h"
#include "config.h"
#include "fetch-object.h"

static struct promisor_remote *promisors;
static struct promisor_remote **promisors_tail = &promisors;

static struct promisor_remote *promisor_remote_new(const char *remote_name)
{
	struct promisor_remote *o;

	o = xcalloc(1, sizeof(*o));
	o->remote_name = xstrdup(remote_name);

	*promisors_tail = o;
	promisors_tail = &o->next;

	return o;
}

static struct promisor_remote *promisor_remote_look_up(const char *remote_name,
						       struct promisor_remote **previous)
{
	struct promisor_remote *o, *p;

	for (p = NULL, o = promisors; o; p = o, o = o->next)
		if (o->remote_name && !strcmp(o->remote_name, remote_name)) {
			if (previous)
				*previous = p;
			return o;
		}

	return NULL;
}

static int promisor_remote_config(const char *var, const char *value, void *data)
{
	const char *name;
	int namelen;
	const char *subkey;

	if (parse_config_key(var, "remote", &name, &namelen, &subkey) < 0)
		return 0;

	if (!strcmp(subkey, "promisor")) {
		char *remote_name;

		if (!git_config_bool(var, value))
			return 0;

		remote_name = xmemdupz(name, namelen);

		if (!promisor_remote_look_up(remote_name, NULL))
			promisor_remote_new(remote_name);

		free(remote_name);
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

struct promisor_remote *promisor_remote_find(const char *remote_name)
{
	promisor_remote_init();

	if (!remote_name)
		return promisors;

	return promisor_remote_look_up(remote_name, NULL);
}

int has_promisor_remote(void)
{
	return !!promisor_remote_find(NULL);
}

int promisor_remote_get_direct(const struct object_id *oids, int oid_nr)
{
	struct promisor_remote *o;

	promisor_remote_init();

	for (o = promisors; o; o = o->next) {
		if (fetch_objects(o->remote_name, oids, oid_nr) < 0)
			continue;
		return 0;
	}

	return -1;
}

