#ifndef EXTERNAL_ODB_H
#define EXTERNAL_ODB_H

#include "odb-helper.h"

extern int has_external_odb(void);
extern struct odb_helper *find_odb_helper(const char *dealer,
					  enum odb_helper_type type);
extern const char *external_odb_root(void);
extern int external_odb_has_object(const unsigned char *sha1);
extern int external_odb_get_object(const unsigned char *sha1);
extern int external_odb_get_direct(const unsigned char *sha1);
extern int external_odb_put_object(const void *buf, size_t len,
				   const char *type, unsigned char *sha1,
				   const char *path);

#endif /* EXTERNAL_ODB_H */
