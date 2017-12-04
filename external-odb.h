#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

extern const char *external_odb_root(void);
extern int external_odb_has_object(const unsigned char *sha1);

#endif /* EXTERNAL_ODB_H */
