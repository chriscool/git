#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "odb-remote.h"

#define ODB_HELPER_CAP_GET_GIT_OBJ    (1u<<0)
#define ODB_HELPER_CAP_GET_RAW_OBJ    (1u<<1)
#define ODB_HELPER_CAP_GET_DIRECT     (1u<<2)
#define ODB_HELPER_CAP_PUT_GIT_OBJ    (1u<<3)
#define ODB_HELPER_CAP_PUT_RAW_OBJ    (1u<<4)
#define ODB_HELPER_CAP_PUT_DIRECT     (1u<<5)
#define ODB_HELPER_CAP_HAVE           (1u<<6)

struct odb_helper {
	const char *name;
	const char *dealer;
	enum odb_helper_type type;
	unsigned int supported_capabilities;
	int initialized;

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
extern int odb_helper_init(struct odb_helper *o);
extern int odb_helper_has_object(struct odb_helper *o,
				 const unsigned char *sha1);
extern int odb_helper_get_object(struct odb_helper *o,
				 const unsigned char *sha1,
				 int fd);
extern int odb_helper_get_direct(struct odb_helper *o,
				 const unsigned char *sha1);

#endif /* ODB_HELPER_H */
