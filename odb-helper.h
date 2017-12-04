#ifndef ODB_HELPER_H
#define ODB_HELPER_H

/*
 * Way to access an odb remote.
 */
struct odb_helper {
	const char *name;       /* from odb.<NAME>.<property> config entries */
	const char *remote;     /* remote storing promised objects */

	struct odb_helper *next;
};

extern struct odb_helper *odb_helper_new(const char *name, int namelen);

#endif /* ODB_HELPER_H */
