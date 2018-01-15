#include "cache.h"
#include "object.h"
#include "argv-array.h"
#include "odb-helper.h"
#include "run-command.h"
#include "sha1-lookup.h"
#include "fetch-object.h"

struct odb_helper *odb_helper_new(const char *name, int namelen)
{
	struct odb_helper *o;

	o = xcalloc(1, sizeof(*o));
	o->name = xmemdupz(name, namelen);

	return o;
}

struct odb_helper_cmd {
	struct argv_array argv;
	struct child_process child;
};

static void odb_helper_load_have(struct odb_helper *o)
{
	if (o->have_valid)
		return;
	o->have_valid = 1;

	/* TODO */
}

static const unsigned char *have_sha1_access(size_t index, void *table)
{
	struct odb_helper_object *have = table;
	return have[index].sha1;
}

static struct odb_helper_object *odb_helper_lookup(struct odb_helper *o,
						   const unsigned char *sha1)
{
	int idx;

	odb_helper_load_have(o);
	idx = sha1_pos(sha1, o->have, o->have_nr, have_sha1_access);
	if (idx < 0)
		return NULL;
	return &o->have[idx];
}

int odb_helper_has_object(struct odb_helper *o, const unsigned char *sha1)
{
	return !!odb_helper_lookup(o, sha1);
}

int odb_helper_get_direct(struct odb_helper *o,
			  const unsigned char *sha1)
{
	int res = 0;
	uint64_t start = getnanotime();

	fetch_object(o->dealer, sha1);

	trace_performance_since(start, "odb_helper_get_direct");

	return res;
}
