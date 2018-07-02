#include "cache.h"
#include "reftable.h"

#define REFTABLE_SIGNATURE 0x52454654  /* "REFT" */

struct reftable_header {
	unsigned int signature: 32;
	unsigned int version_number: 8;
	unsigned int block_size: 24;
	uint64_t min_update_index;
	uint64_t max_update_index;
};

#define WRITE_BUFFER_SIZE 8192
static unsigned char write_buffer[WRITE_BUFFER_SIZE];
static unsigned long write_buffer_len;

static int reftable_write_flush(int fd)
{
	unsigned int buffered = write_buffer_len;
	if (buffered) {
		if (write_in_full(fd, write_buffer, buffered) < 0)
			return -1;
		write_buffer_len = 0;
	}
	return 0;
}

static int reftable_write_data(int fd, void *data, unsigned int len)
{
	while (len) {
		unsigned int buffered = write_buffer_len;
		unsigned int partial = WRITE_BUFFER_SIZE - buffered;
		if (partial > len)
			partial = len;
		memcpy(write_buffer + buffered, data, partial);
		buffered += partial;
		if (buffered == WRITE_BUFFER_SIZE) {
			write_buffer_len = buffered;
			if (reftable_write_flush(fd))
				return -1;
			buffered = 0;
		}
		write_buffer_len = buffered;
		len -= partial;
		data = (char *) data + partial;
	}
	return 0;
}

static int reftable_write_header(int fd, struct reftable_header *header)
{
	return reftable_write_data(fd, header, sizeof(*header));
}

void reftable_header_init(struct reftable_header *header, uint32_t block_size,
			  uint64_t min_update_index, uint64_t max_update_index)
{
	header->signature = htonl(REFTABLE_SIGNATURE);
	header->version_number = htonl(1);

	if (block_size > 0xffffff)
		BUG("too big block size '%d'", block_size);
	header->block_size = htonl(block_size);

	header->min_update_index = htonl(min_update_index);
	header->max_update_index = htonl(max_update_index);
}

