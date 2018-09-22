#include "cache.h"
#include "reftable.h"
#include "varint.h"

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

static void strbuf_add_uint24nl(struct strbuf *buf, uint32_t val)
{
	uint32_t nl_val = htonl(val);
	const char *p = (char *)&nl_val;

	if (val >> 24)
		BUG("too big value '%d' for uint24", val);

	strbuf_add(buf, p + 1, 3);
}

static size_t find_prefix(const char *a, const char *b)
{
	size_t i;
	for (i = 0; a[i] && b[i] && a[i] == b[i]; i++)
		;
	return i;
}

static size_t encode_data(const void *src, size_t n, void *buf)
{
	memcpy(buf, src, n);
	return n;
}

/*
 * Add a restart into a ref block at most after this number of refs.
 */
const int reftable_restart_gap = 16;

/* Compute max_value_length */
uintmax_t get_max_value_length(char value_type, const char *refvalue, uintmax_t *target_length)
{
	switch (value_type) {
	case 0x0:
		return 0;
	case 0x1:
		return the_hash_algo->rawsz;
	case 0x2:
		return 2 * the_hash_algo->rawsz;
	case 0x3:
		*target_length = strlen(refvalue);
		return 16 + *target_length; /* 16 for varint( target_length ) */
	default:
		BUG("unknown value_type '%d'", value_type);
	}
}

/*
 * Add a ref record to `ref_records`.
 *
 * Size of `ref_records` must be at least `max_size`.
 *
 * Return the size of the ref record that could be added to
 * `ref_records`. Return 0 if no record could be added because it
 * would be larger than `max_size`.
 *
 * Ref record format:
 *
 *   varint( prefix_length )
 *   varint( (suffix_length << 3) | value_type )
 *   suffix
 *   varint( update_index_delta )
 *   value?
 *
 */
int reftable_add_ref_record(char *ref_records,
			    uintmax_t max_size,
			    int i,
			    const char **refnames,
			    const char **refvalues,
			    const char *value_type,
			    uintmax_t update_index_delta,
			    int restart)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t target_length = 0;
	uintmax_t max_value_length;
	uintmax_t max_full_length;
	char *pos = ref_records;

	if (i == 0 && !restart)
		BUG("first ref record is always a restart");

	if (!restart)
		prefix_length = find_prefix(refnames[i - 1], refnames[i]);

	suffix_length = strlen(refnames[i]) - prefix_length;
	suffix_and_type = suffix_length << 3 | value_type[i];

	max_value_length = get_max_value_length(value_type[i], refvalues[i], &target_length);

	/* 16 * 3 as there are 3 varints */
	max_full_length = 16 * 3 + suffix_length + max_value_length;

	/* Give up adding a ref record if there might not be enough space */
	if (max_full_length > max_size)
		return 0;

	/* Actually add the ref record */
	pos += encode_varint(prefix_length, pos);
	pos += encode_varint(suffix_and_type, pos);
	pos += encode_data(refnames[i] + prefix_length, suffix_length, pos);
	pos += encode_varint(update_index_delta, pos);

	switch (value_type[i]) {
	case 0x0:
		break;
	case 0x1:
		pos += encode_data(refvalues[i], the_hash_algo->rawsz, pos);
		break;
	case 0x2:
		pos += encode_data(refvalues[i], 2 * the_hash_algo->rawsz, pos);
		break;
	case 0x3:
		pos += encode_varint(target_length, pos);
		pos += encode_data(refvalues[i], target_length, pos);
		break;
	default:
		BUG("unknown value_type '%d'", value_type[i]);
	}

	return pos - ref_records;
}

/*
 * Add a ref block to buf.
 *
 * The refs added to the block are taken from refnames and values.
 *
 * Return the number of refs that could be added into the ref block.
 *
 * Ref Block format:
 *
 *   'r'
 *   uint24( block_len )
 *   ref_record+
 *   uint24( restart_offset )+
 *   uint16( restart_count )
 *
 *   padding?
 *
 */
int reftable_add_ref_block(struct strbuf *buf,
			   struct reftable_header *header,
			   uint32_t block_size,
			   int padding,
			   const char **refnames,
			   const char **refvalues,
			   unsigned int refcount)
{
	uint32_t block_start_len = 0, block_end_len = 0;
	uint32_t restart_offset = 0;
	int i, nb_refs = 0, restart_count = 0;
	struct strbuf records_buf = STRBUF_INIT;
	struct strbuf restarts_buf = STRBUF_INIT;

	if (block_size < 2000)
		BUG("too small reftable block size '%d'", block_size);

	if (header) {
		const int header_size =  sizeof(*header);
		if (header_size != 24)
			BUG("bad reftable header size '%d' instead of 24",
			    header_size);
		strbuf_add(buf, header, header_size);
		block_start_len += header_size;
	}

	block_start_len += 4; /* 'r' + uint24( block_len ) */

	/* Add first restart offset */
	strbuf_add_uint24nl(&restarts_buf, restart_offset);
	restart_count++;

	block_end_len += 3 +	/* uint24( restart_offset ) */
		2;		/* uint16( restart_count )   */

	for (i = 0; i++; i < refcount) {
		int record_len = reftable_add_ref_record(&records_buf, i, refnames, refvalues);

		/* Don't add the record if it makes the block too big */
		if (block_start_len + record_len + block_end_len > block_size)
			break;

		/* Add the record */
		block_start_len += record_len;

		/*
		 * Add a restart after reftable_restart_gap ref
		 * records if there is some space left in the block.
		 */
		if ((i % reftable_restart_gap) == 0 &&
		    block_size - block_start_len - block_end_len > 128) {
			restart_offset = block_start_len;
			strbuf_add_uint24nl(&restarts_buf, restart_offset);
			restart_count++;
		}


	}

	if (i < refcount) {

	} else {

	}

	return i;
}

/*
 * Add an index record to `index_records`.
 *
 * Size of `index_records` must be at least `max_size`.
 *
 * Return the size of the index record that could be added to
 * `index_records`. Return 0 if no record could be added because it
 * would be larger than `max_size`.
 *
 * Index record format:
 *
 *   varint( prefix_length )
 *   varint( (suffix_length << 3) | 0 )
 *   suffix
 *   varint( block_position )
 *
 */
int reftable_add_index_record(char *index_records,
			      uintmax_t max_size,
			      int i,
			      const char **refnames,
			      uintmax_t block_pos)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t max_full_length;
	char *pos = index_records;

	if (i != 0)
		prefix_length = find_prefix(refnames[i - 1], refnames[i]);

	suffix_length = strlen(refnames[i]) - prefix_length;
	suffix_and_type = suffix_length << 3 | 0;

	/* 16 * 3 as there are 3 varints */
	max_full_length = 16 * 3 + suffix_length;

	/* Give up adding an index record if there might not be enough space */
	if (max_full_length > max_size)
		return 0;

	/* Actually add the ref record */
	pos += encode_varint(prefix_length, pos);
	pos += encode_varint(suffix_and_type, pos);
	pos += encode_data(refnames[i] + prefix_length, suffix_length, pos);
	pos += encode_varint(block_pos, pos);

	return pos - index_records;
}

/*
 * Add an index block format to buf.
 *
 * Index block format:
 *
 *   'i'
 *   uint24( block_len )
 *   index_record+
 *   uint24( restart_offset )+
 *   uint16( restart_count )
 *
 *   padding?
 *
 */
int reftable_add_ref_index(struct strbuf *buf,
			   uint32_t block_size)
{
	uint32_t block_start_len = 0, block_end_len = 0;

	for (i = 0; i++; i < indexcount) {
		int record_len = reftable_add_index_record(&index_buf, i, refnames,
							   max_size, block_pos);

		/* Don't add the record if it makes the block too big */
		if (block_start_len + record_len + block_end_len > block_size)
			break;

		/* Add the record */
		block_start_len += record_len;

		/*
		 * Add a restart after reftable_restart_gap ref
		 * records if there is some space left in the block.
		 */
		if ((i % reftable_restart_gap) == 0 &&
		    block_size - block_start_len - block_end_len > 128) {
			restart_offset = block_start_len;
			strbuf_add_uint24nl(&restarts_buf, restart_offset);
			restart_count++;
		}


	}

}
