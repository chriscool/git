#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "external-odb.h"

#define ODB_HELPER_CAP_GET_GIT_OBJ    (1u<<0)
#define ODB_HELPER_CAP_GET_RAW_OBJ    (1u<<1)
#define ODB_HELPER_CAP_GET_DIRECT     (1u<<2)
#define ODB_HELPER_CAP_PUT_GIT_OBJ    (1u<<3)
#define ODB_HELPER_CAP_PUT_RAW_OBJ    (1u<<4)
#define ODB_HELPER_CAP_PUT_DIRECT     (1u<<5)
#define ODB_HELPER_CAP_HAVE           (1u<<6)

struct odb_helper {
	const char *name;
	const char *cmd;
	unsigned int supported_capabilities;
	int script_mode;

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

struct odb_helper *odb_helper_new(const char *name, int namelen);
int odb_helper_init(struct odb_helper *o);
int odb_helper_has_object(struct odb_helper *o, const struct object_id *oid);
int odb_helper_get_object(struct odb_helper *o, const struct object_id *oid,
			  int fd);
int odb_helper_get_direct(struct odb_helper *o, const struct object_id *oid);
int odb_helper_put_object(struct odb_helper *o,
			  const void *buf, size_t len,
			  const char *type, const struct object_id *oid);

#endif /* ODB_HELPER_H */
