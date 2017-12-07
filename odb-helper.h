#ifndef ODB_HELPER_H
#define ODB_HELPER_H

struct odb_helper {
	const char *name;
	const char *dealer;

	struct odb_helper *next;
};

extern struct odb_helper *odb_helper_new(const char *name, int namelen);
extern int odb_helper_get_direct(struct odb_helper *o,
				 const unsigned char *sha1);
#endif /* ODB_HELPER_H */
