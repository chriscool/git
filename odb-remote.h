#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern int has_odb_remote(void);
extern const char *odb_remote_root(void);
extern int odb_remote_has_object(const unsigned char *sha1);
extern int odb_remote_get_direct(const unsigned char *sha1);

#endif /* ODB_REMOTE_H */
