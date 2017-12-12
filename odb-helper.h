#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "remote-odb.h"

/*
 * An odb helper is a way to access a remote odb.
 *
 * Information in its fields comes from the config file [odb "NAME"]
 * entries.
 */
struct odb_helper {
	const char *name;                 /* odb.<NAME>.* */
	const char *remote;               /* odb.<NAME>.promisorRemote */
	const char *partial_clone_filter; /* odb.<NAME>.partialCloneFilter */
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
extern int odb_helper_get_direct(struct odb_helper *o,
				 const struct object_id *oids,
				 int oid_nr);

#endif /* ODB_HELPER_H */
