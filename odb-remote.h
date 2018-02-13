#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern const char *odb_remote_root(void);
extern int odb_remote_get_direct(const unsigned char *sha1);
extern int odb_remote_get_many_direct(const struct oid_array *to_get);

#endif /* ODB_REMOTE_H */
