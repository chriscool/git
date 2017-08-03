#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

const char *external_odb_root(void);
int external_odb_has_object(const struct object_id *oid);
int external_odb_get_object(const struct object_id *oid);
int external_odb_get_direct(const struct object_id *oid);
int external_odb_put_object(const void *buf, size_t len,
			    const char *type, const struct object_id *oid,
			    const char *path);

#endif /* EXTERNAL_ODB_H */
