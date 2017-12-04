#ifndef ODB_HELPER_H
#define ODB_HELPER_H

struct odb_helper {
	const char *name;
	const char *dealer;

	struct odb_helper *next;
};

extern struct odb_helper *odb_helper_new(const char *name, int namelen);

#endif /* ODB_HELPER_H */
