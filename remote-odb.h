#ifndef REMOTE_ODB_H
#define REMOTE_ODB_H

extern void remote_odb_reinit(void);
extern struct odb_helper *find_odb_helper(const char *remote);
extern int has_remote_odb(void);
extern int remote_odb_get_direct(const struct object_id *oids, int oid_nr);

#endif /* REMOTE_ODB_H */
