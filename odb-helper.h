#ifndef ODB_HELPER_H
#define ODB_HELPER_H

#include "external-odb.h"

enum odb_helper_fetch_kind {
	ODB_FETCH_KIND_PLAIN_OBJECT = 0,
	ODB_FETCH_KIND_GIT_OBJECT,
	ODB_FETCH_KIND_FAULT_IN
};

#define ODB_HELPER_CAP_GET    (1u<<0)
#define ODB_HELPER_CAP_PUT    (1u<<1)
#define ODB_HELPER_CAP_HAVE   (1u<<2)

struct odb_helper {
	const char *name;
	const char *cmd;
	unsigned int supported_capabilities;
	enum odb_helper_fetch_kind fetch_kind;
	int script_mode;

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

struct odb_helper *odb_helper_new(const char *name, int namelen);
int odb_helper_init(struct odb_helper *o);
int odb_helper_has_object(struct odb_helper *o, const unsigned char *sha1);
int odb_helper_fetch_object(struct odb_helper *o, const unsigned char *sha1,
			    int fd);
int odb_helper_fault_in_object(struct odb_helper *o, const unsigned char *sha1);
int odb_helper_for_each_object(struct odb_helper *o,
			       each_external_object_fn, void *);
int odb_helper_write_object(struct odb_helper *o,
			    const void *buf, size_t len,
			    const char *type, unsigned char *sha1);

#endif /* ODB_HELPER_H */
