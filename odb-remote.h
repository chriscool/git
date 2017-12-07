#ifndef ODB_REMOTE_H
#define ODB_REMOTE_H

extern int has_external_odb(void);
extern const char *external_odb_root(void);
extern int external_odb_has_object(const unsigned char *sha1);

#endif /* ODB_REMOTE_H */
