#ifndef REFS_PACKED2_BACKEND_H
#define REFS_PACKED2_BACKEND_H

/*
 * Support for storing references in a `packed-refs2` file.
 *
 * Note that this backend doesn't check for D/F conflicts, because it
 * doesn't care about them. But usually it should be wrapped in a
 * `files_ref_store` that prevents D/F conflicts from being created,
 * even among packed refs.
 */

struct ref_store *packed_ref2_store_create(const char *path,
					   unsigned int store_flags);

/*
 * Lock the packed-refs2 file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, write
 * an error message to `err` and return a nonzero value.
 */
int packed_refs2_lock(struct ref_store *ref_store, int flags, struct strbuf *err);

void packed_refs2_unlock(struct ref_store *ref_store);
int packed_refs2_is_locked(struct ref_store *ref_store);

/*
 * Return true if `transaction` really needs to be carried out against
 * the specified packed_ref2_store, or false if it can be skipped
 * (i.e., because it is an obvious NOOP). `ref_store` must be locked
 * before calling this function.
 */
int is_packed2_transaction_needed(struct ref_store *ref_store,
				  struct ref_transaction *transaction);

#endif /* REFS_PACKED2_BACKEND_H */
