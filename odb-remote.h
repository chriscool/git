#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern struct odb_helper *find_odb_helper(const char *dealer);
extern int has_odb_remote(void);
extern const char *odb_remote_root(void);
extern int odb_remote_get_direct(const unsigned char *sha1);

#endif /* ODB_REMOTE_H */
