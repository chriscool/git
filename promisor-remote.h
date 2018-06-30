#ifndef PROMISOR_REMOTE_H
#define PROMISOR_REMOTE_H

/*
 * Promisor remote linked list
 * Its information come from remote.XXX config entries.
 */
struct promisor_remote {
	const char *remote_name;
	struct promisor_remote *next;
};

extern struct promisor_remote *promisor_remote_new(const char *remote_name);
extern struct promisor_remote *promisor_remote_find(const char *remote_name);
extern int has_promisor_remote(void);

#endif /* PROMISOR_REMOTE_H */
