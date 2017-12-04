#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "odb-remote.h"

struct odb_helper {
	const char *name;
	const char *dealer;
	enum odb_helper_type type;

	struct odb_helper_object {
		struct object_id oid;
		unsigned long size;
		enum object_type type;
	} *have;
	int have_nr;
	int have_alloc;
	int have_valid;

	struct odb_helper *next;
};

extern struct odb_helper *odb_helper_new(const char *name, int namelen);
extern int odb_helper_has_object(struct odb_helper *o,
				 const unsigned char *sha1);
extern int odb_helper_get_object(struct odb_helper *o,
				 const unsigned char *sha1,
				 int fd);
extern int odb_helper_get_direct(struct odb_helper *o,
				 const unsigned char *sha1);
extern int odb_helper_get_many_direct(struct odb_helper *o,
				      const struct oid_array *to_get);

#endif /* ODB_HELPER_H */
