#ifndef ODB_HELPER_H
#define ODB_HELPER_H

struct odb_helper {
	const char *name;
	const char *dealer;

	struct odb_helper_object {
		unsigned char sha1[20];
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
				 const unsigned char *sha1);
#endif /* ODB_HELPER_H */
