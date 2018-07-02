#ifndef REFTABLE_H
#define REFTABLE_H

struct reftable_header;

void reftable_header_init(struct reftable_header *header, uint32_t block_size,
			  uint64_t min_update_index, uint64_t max_update_index);

#endif /* REFTABLE_H */



