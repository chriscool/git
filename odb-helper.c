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

int odb_helper_get_direct(struct odb_helper *o,
			  const struct object_id *oids,
			  int oid_nr)
{
	int res;
	uint64_t start = getnanotime();

	res = fetch_objects(o->remote, oids, oid_nr);

	trace_performance_since(start, "odb_helper_get_direct");

	return res;
}
