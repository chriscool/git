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
			  const unsigned char *sha1)
{
	int res = 0;
	uint64_t start = getnanotime();

	fetch_object(o->dealer, sha1);

	trace_performance_since(start, "odb_helper_get_direct");

	return res;
}

int odb_helper_get_many_direct(struct odb_helper *o,
			       const struct oid_array *to_get)
{
	int res = 0;
	uint64_t start;

	start = getnanotime();

	fetch_objects(o->dealer, to_get);

	trace_performance_since(start, "odb_helper_get_many_direct");

	return res;
}
