#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

const char *external_odb_root(void);
int external_odb_has_object(const unsigned char *sha1);
int external_odb_fetch_object(const unsigned char *sha1);

#endif /* EXTERNAL_ODB_H */
