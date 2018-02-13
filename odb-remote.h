#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern struct odb_helper *find_odb_helper(const char *dealer);
extern int has_odb_remote(void);
extern int odb_remote_get_direct(const unsigned char *sha1);
extern int odb_remote_get_many_direct(const struct oid_array *to_get);

#endif /* ODB_REMOTE_H */
