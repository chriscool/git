#ifndef REMOTE_ODB_H
#define REMOTE_ODB_H

enum odb_helper_type {
	ODB_HELPER_ANY = 0,
	ODB_HELPER_GIT_REMOTE,
	ODB_HELPER_SCRIPT_CMD,
	ODB_HELPER_SUBPROCESS_CMD,
	OBJ_HELPER_MAX
};

extern void remote_odb_reinit(void);
extern struct odb_helper *find_odb_helper(const char *remote,
					  enum odb_helper_type type);
extern int has_remote_odb(void);
extern const char *remote_odb_root(void);
extern int remote_odb_has_object(const unsigned char *sha1);
extern int remote_odb_get_object(const unsigned char *sha1);
extern int remote_odb_get_direct(const struct object_id *oids, int oid_nr);

#endif /* REMOTE_ODB_H */
