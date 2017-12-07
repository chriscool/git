#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern const char *odb_remote_root(void);
extern int odb_remote_get_direct(const unsigned char *sha1);

#endif /* ODB_REMOTE_H */
