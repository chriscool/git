#ifndef REMOTE_ODB_H
#define REMOTE_ODB_H

extern struct odb_helper *find_odb_helper(const char *remote);
extern int has_remote_odb(void);

#endif /* REMOTE_ODB_H */
