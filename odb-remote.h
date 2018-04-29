#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

enum odb_helper_type {
	ODB_HELPER_NONE = 0,
	ODB_HELPER_GIT_REMOTE,
	ODB_HELPER_SCRIPT_CMD,
	ODB_HELPER_SUBPROCESS_CMD,
	OBJ_HELPER_MAX
};

extern void odb_remote_reinit(void);
extern struct odb_helper *find_odb_helper(const char *dealer);
extern int has_odb_remote(void);
extern const char *odb_remote_root(void);
extern int odb_remote_has_object(const unsigned char *sha1);
extern int odb_remote_get_direct(const unsigned char *sha1);
extern int odb_remote_get_many_direct(const struct oid_array *to_get);

#endif /* ODB_REMOTE_H */
