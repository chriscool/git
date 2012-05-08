#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

const char *external_odb_root(void);
int external_odb_has_object(const unsigned char *sha1);
int external_odb_fetch_object(const unsigned char *sha1);

typedef int (*each_external_object_fn)(const unsigned char *sha1,
				       enum object_type type,
				       unsigned long size,
				       void *data);
int external_odb_for_each_object(each_external_object_fn, void *);

#endif /* EXTERNAL_ODB_H */
