#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "remote-odb.h"

/*
 * Way to access an odb remote.
 */
struct odb_helper {
	const char *name;       /* from odb.<NAME>.<property> config entries */
	const char *remote;     /* remote storing promised objects */
	const char *command;    /* script command */
	const char *partial_clone_filter; /* odb.<name>.partialCloneFilter */
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
