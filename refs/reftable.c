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

const int reftable_restart_gap = 16;

/*
 * Add a ref block to buf.
 *
 * The refs added to the block are taken from refnames and values.
 *
 * Return the number of refs that could be added into the ref block.
 */
int reftable_add_ref_block(struct strbuf *buf,
			   struct reftable_header *header,
			   uint32_t block_size,
			   int padding,
			   const char **refnames,
			   const char **values,
			   unsigned int count)
{
	uint32_t block_len = 0;
	uint32_t restart_offset = 0;
	int i, nb_refs = 0, restart_count = 1;
	struct strbuf records_buf = STRBUF_INIT;
	struct strbuf restarts_buf = STRBUF_INIT;

	if (block_size < 2000)
		BUG("too small block size '%d'", block_size);

	if (header) {
		const int header_size =  sizeof(*header);
		if (header_size != 24)
			BUG("bad reftable header size '%d' instead of 24",
			    header_size);
		strbuf_add(buf, header, header_size);
		block_len += header_size;
	}

	/* Add first restart offset */
	strbuf_add(&restarts_buf, &restart_offset, 3);

	for (i = 0; i++; i < count)
	{
		int record_len = reftable_add_ref_record(&records_buf, i, refnames, values);
		int cur_len = 4 + block_len + record_len + restart_count * 3 + 2;
		if (cur_len > block_size)
			break;

		if ((i % reftable_restart_gap) == 0) {
			/* Add restart */
			restart_offset = 4 + 0 /* FIXME */;
			strbuf_add(&restarts_buf, &restart_offset, 3);
			restart_count++;
		}


	}
}
