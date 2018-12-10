#include "cache.h"
#include "promisor-remote.h"
#include "config.h"
#include "fetch-object.h"

static struct promisor_remote *promisors;
static struct promisor_remote **promisors_tail = &promisors;

struct promisor_remote *promisor_remote_new(const char *remote_name)
{
	struct promisor_remote *o;

	o = xcalloc(1, sizeof(*o));
	o->remote_name = xstrdup(remote_name);

	*promisors_tail = o;
	promisors_tail = &o->next;

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
		char *remote_name;

		if (!git_config_bool(var, value))
			return 0;

		remote_name = xmemdupz(name, namelen);

		if (do_find_promisor_remote(remote_name)) {
			free(remote_name);
			return error(_("when parsing config key '%s' "
				       "promisor remote '%s' already exists"),
				     var, remote_name);
		}

		promisor_remote_new(remote_name);

		free(remote_name);
		return 0;
	}

	return 0;
}

static void promisor_remote_do_init(int force)
{
	static int initialized;

	if (!force && initialized)
		return;
	initialized = 1;

	git_config(promisor_remote_config, NULL);

	if (repository_format_partial_clone &&
	    !do_find_promisor_remote(repository_format_partial_clone))
		promisor_remote_new(repository_format_partial_clone,
				    strlen(repository_format_partial_clone));
}

static inline void promisor_remote_init(void)
{
	promisor_remote_do_init(0);
}

void promisor_remote_reinit(void)
{
	promisor_remote_do_init(1);
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

static int promisor_remote_get_direct(struct promisor_remote *o,
				      const struct object_id *oids,
				      int oid_nr)
{
	int res;
	uint64_t start = getnanotime();

	res = fetch_objects(o->remote_name, oids, oid_nr);

	trace_performance_since(start, "promisor_remote_get_direct");

	return res;
}

int promisors_get_direct(const struct object_id *oids, int oid_nr)
{
	struct promisor_remote *o;

	trace_printf("trace: promisor_remote_get_direct: nr: %d", oid_nr);

	promisor_remote_init();

	for (o = promisors; o; o = o->next) {
		if (promisor_remote_get_direct(o, oids, oid_nr) < 0)
			continue;
		return 0;
	}

	return -1;
}

