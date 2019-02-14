#ifndef REFTABLE_H
#define REFTABLE_H

struct reftable_header;
struct ref_update;

int reftable_write_reftable_blocks(int fd, uint32_t block_size, const char *path,
				   const struct ref_update **updates, int nr_updates);
int reftable_read_reftable_blocks(int fd, uint32_t block_size, const char *path,
				  const struct ref_update **updates,
				  int *nr_updates, int *alloc_updates);

#endif /* REFTABLE_H */



