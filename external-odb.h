#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

enum odb_helper_type {
	ODB_HELPER_NONE = 0,
	ODB_HELPER_GIT_REMOTE,
	ODB_HELPER_SCRIPT_CMD,
	ODB_HELPER_SUBPROCESS_CMD,
	OBJ_HELPER_MAX
};

extern void external_odb_reinit(void);
extern int has_external_odb(void);
extern struct odb_helper *find_odb_helper(const char *dealer,
					  enum odb_helper_type type);
extern const char *external_odb_root(void);
extern int external_odb_has_object(const unsigned char *sha1);
extern int external_odb_get_object(const unsigned char *sha1);
extern int external_odb_get_direct(const unsigned char *sha1);
extern int external_odb_put_object(const void *buf, size_t len,
				   const char *type, unsigned char *sha1);
extern int external_odb_get_many_direct(const struct oid_array *to_get);

#endif /* EXTERNAL_ODB_H */
