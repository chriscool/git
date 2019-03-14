#ifndef REFTABLE_H
#define REFTABLE_H

struct reftable_header;
struct ref_update;
struct ref_update_array;

int reftable_write_reftable_blocks(int fd, uint32_t block_size, const char *path,
				   struct ref_update_array *update_array, int padding);
int reftable_read_reftable_blocks(int fd, uint32_t block_size, const char *path,
				  struct ref_update_array *update_array);

#endif /* REFTABLE_H */



