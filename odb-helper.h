#ifndef ODB_HELPER_H
#define ODB_HELPER_H

/*
 * Way to access an odb remote.
 */
struct odb_helper {
	const char *name;       /* from odb.<NAME>.<property> config entries */
	const char *remote;     /* remote storing promised objects */
	const char *partial_clone_filter; /* odb.<name>.partialCloneFilter */

	struct odb_helper *next;
};

extern struct odb_helper *odb_helper_new(const char *name, int namelen);
extern int odb_helper_get_direct(struct odb_helper *o,
				 const unsigned char *sha1);
extern int odb_helper_get_many_direct(struct odb_helper *o,
				      const struct oid_array *to_get);

#endif /* ODB_HELPER_H */
