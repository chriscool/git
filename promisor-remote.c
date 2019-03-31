#include "cache.h"
#include "promisor-remote.h"
#include "config.h"
#include "fetch-object.h"

static struct promisor_remote *promisors;
static struct promisor_remote **promisors_tail = &promisors;

static struct promisor_remote *promisor_remote_new(const char *remote_name)
{
	struct promisor_remote *r;

	if (*remote_name == '/') {
		warning(_("promisor remote name cannot begin with '/': %s"),
			remote_name);
		return NULL;
	}

	FLEX_ALLOC_STR(r, name, remote_name);

	*promisors_tail = r;
	promisors_tail = &r->next;

	return r;
}

static struct promisor_remote *promisor_remote_lookup(const char *remote_name,
						      struct promisor_remote **previous)
{
	struct promisor_remote *r, *p;

	for (p = NULL, r = promisors; r; p = r, r = r->next)
		if (r->name && !strcmp(r->name, remote_name)) {
			if (previous)
				*previous = p;
			return r;
		}

	return NULL;
}

static void promisor_remote_move_to_tail(struct promisor_remote *r,
					 struct promisor_remote *previous)
{
	if (previous)
		previous->next = r->next;
	else
		promisors = r->next ? r->next : r;
	r->next = NULL;
	*promisors_tail = r;
	promisors_tail = &r->next;
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

		if (!promisor_remote_lookup(remote_name, NULL))
			promisor_remote_new(remote_name);

		free(remote_name);
		return 0;
	}
	if (!strcmp(subkey, "partialclonefilter")) {
		struct promisor_remote *r;
		char *remote_name = xmemdupz(name, namelen);

		r = promisor_remote_lookup(remote_name, NULL);
		if (!r)
			r = promisor_remote_new(remote_name);

		free(remote_name);
		return git_config_string(&r->partial_clone_filter, var, value);
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

	if (repository_format_partial_clone) {
		struct promisor_remote *o, *previous;

		o = promisor_remote_lookup(repository_format_partial_clone,
					   &previous);
		if (o)
			promisor_remote_move_to_tail(o, previous);
		else
			promisor_remote_new(repository_format_partial_clone);
	}
}

static inline void promisor_remote_init(void)
{
	promisor_remote_do_init(0);
}

void promisor_remote_reinit(void)
{
	promisor_remote_do_init(1);
}

struct promisor_remote *promisor_remote_find(const char *remote_name)
{
	promisor_remote_init();

	if (!remote_name)
		return promisors;

	return promisor_remote_lookup(remote_name, NULL);
}

int has_promisor_remote(void)
{
	return !!promisor_remote_find(NULL);
}

int promisor_remote_get_direct(const struct object_id *oids, int oid_nr)
{
	struct promisor_remote *r;

	promisor_remote_init();

	for (r = promisors; r; r = r->next) {
		if (fetch_objects(r->name, oids, oid_nr) < 0)
			continue;
		return 0;
	}

	return -1;
}

