#include "cache.h"
#include "refs.h"
#include "refs/refs-internal.h"
#include "refs/reftable.h"
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

static void encode_padding(size_t n, void *buf)
{
	memset(buf, 0, n);
}

static size_t encode_uint16nl(uint16_t val, void *buf)
{
	uint16_t nl_val = htonl(val);
	const char *p = (char *)&nl_val;

	return encode_data(p, 2, buf);
}

static size_t encode_uint24nl(uint32_t val, void *buf)
{
	uint32_t nl_val = htonl(val);
	const char *p = (char *)&nl_val;

	if (val >> 24)
		BUG("too big value '%d' for uint24", val);

	return encode_data(p + 1, 3, buf);
}

static size_t encode_reftable_header(struct reftable_header *header, void *buf)
{
	const int header_size =  sizeof(*header);

	if (!header)
		return 0;

	if (header_size != 24)
		BUG("bad reftable header size '%d' instead of 24", header_size);

	return encode_data(header, header_size, buf);
}

/*
 * Add a restart into a ref block at most after this number of refs.
 */
const int reftable_restart_gap = 16;

/* Compute max_value_length */
uintmax_t get_max_value(int value_type, const struct ref_update *update,
			const char **refvalue, uintmax_t *target_length)
{
	switch (value_type) {
	case 0x0:
		return 0;
	case 0x1:
		return the_hash_algo->rawsz;
	case 0x2:
		return 2 * the_hash_algo->rawsz;
	case 0x3:
		BUG("symrefs are not supported yet in reftable (refname: '%s')",
		    update->refname);
		return 16 /* + *target_length */ ; /* 16 for varint( target_length ) */
	default:
		BUG("unknown value_type '%d'", value_type);
	}
}

int get_value_type(const struct ref_update *update, struct object_id *peeled)
{
	enum peel_status status;

	if (!(update->flags & REF_HAVE_NEW))
		return 0x0; /* deletion */

	status = peel_object(&update->new_oid, peeled);

	switch (status) {
	case PEEL_PEELED:
		return 0x2; /* 2 oids */
	case PEEL_NON_TAG:
		return 0x1; /* 1 oid */
	case PEEL_IS_SYMREF:
		return 0x3; /* symref */
	case PEEL_INVALID:
		return -1;
	case PEEL_BROKEN:
		return -2;
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
			    const struct ref_update *updates,
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
	const char *refname = updates[i].refname;
	const char *refvalue = NULL;
	int value_type;
	struct object_id peeled;

	if (i == 0 && !restart)
		BUG("first ref record is always a restart");

	value_type = get_value_type(&updates[i], &peeled);
	if (value_type < 0)
		return value_type;

	if (!restart)
		prefix_length = find_prefix(updates[i - 1].refname, refname);

	suffix_length = strlen(updates[i].refname) - prefix_length;
	suffix_and_type = suffix_length << 3 | value_type;

	max_value_length = get_max_value(value_type, &updates[i], &refvalue, &target_length);

	if (value_type && !refvalue)
		BUG("couldn't find value for ref '%s'", updates[i].refname);

	/* 16 * 3 as there are 3 varints */
	max_full_length = 16 * 3 + suffix_length + max_value_length;

	/* Give up adding a ref record if there might not be enough space */
	if (max_full_length > max_size)
		return 0;

	/* Actually add the ref record */
	pos += encode_varint(prefix_length, pos);
	pos += encode_varint(suffix_and_type, pos);
	pos += encode_data(refname + prefix_length, suffix_length, pos);
	pos += encode_varint(update_index_delta, pos);

	switch (value_type) {
	case 0x0:
		break;
	case 0x1:
		pos += encode_data(refvalue, the_hash_algo->rawsz, pos);
		break;
	case 0x2:
		pos += encode_data(refvalue, 2 * the_hash_algo->rawsz, pos);
		break;
	case 0x3:
		pos += encode_varint(target_length, pos);
		pos += encode_data(refvalue, target_length, pos);
		break;
	default:
		BUG("unknown value_type '%d'", value_type);
	}

	return pos - ref_records;
}

uintmax_t get_update_index_delta(const struct ref_update *update)
{
	/* TODO: compute update_index_delta from update */

	return 0;
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
int reftable_add_ref_block(char *ref_records,
			   struct reftable_header *header,
			   uint32_t block_size,
			   int padding,
			   const struct ref_update *updates,
			   int nr_updates)
{
	uint32_t block_start_len = 0, block_end_len = 0;
	uint32_t restart_offset = 0;
	int i, nb_refs = 0, restart_count = 0;
	char *ref_restarts;
	char *block_len_pos;

	if (block_size < 2000)
		BUG("too small reftable block size '%d'", block_size);

	/*
	 * For now let's allocate ref_restarts.
	 * TODO: reuse a block for ref_restarts, and/or:
	 * TODO: optimize size allocated for ref_restarts
	 */
	ref_restarts = xcalloc(1, block_size);

	/* Add header */
	block_start_len += encode_reftable_header(header, ref_records + block_start_len);

	/* Add 'r' + uint24( block_len ) */
	block_start_len += encode_data("r", 1, ref_records + block_start_len);
	/* We don't know the block_len so we postpone writting it */
	block_len_pos = ref_records + block_start_len;
	block_start_len += 3;

	/* Add first restart offset */
	block_end_len += encode_uint24nl(block_start_len, ref_restarts + block_end_len);
	restart_count++;

	for (i = 0; i++; i < nr_updates) {
		int restart = ((i % reftable_restart_gap) == 0);		
		int max_size = block_size - (block_start_len + block_end_len + 2);
		uintmax_t update_index_delta = get_update_index_delta(&updates[i]);
		int record_len = reftable_add_ref_record(ref_records, max_size,
							 i, updates, update_index_delta, restart);

		if (record_len < 1)
			break;

		/* Add the record */
		block_start_len += record_len;

		/*
		 * Add a restart after reftable_restart_gap ref
		 * records if there is some space left in the block.
		 */
		if (restart && block_size - (block_start_len + block_end_len + 2) > 3) {
			block_end_len += encode_uint24nl(block_start_len, ref_restarts + block_end_len);
			restart_count++;
		}
	}

	/* Add restart count */
	block_end_len += encode_uint16nl(restart_count, ref_restarts + block_end_len);

	/* Copy restarts into the records block */
	block_start_len += encode_data(ref_restarts, block_end_len, ref_records + block_start_len);

	free(ref_restarts);

	/* Write block_len at the beginning of the block */
	encode_uint24nl(block_start_len, block_len_pos);

	/* Add padding */
	encode_padding(block_size - block_start_len, ref_records + block_start_len);

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
			      const struct ref_update *updates,
			      uintmax_t block_pos)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t max_full_length;
	char *pos = index_records;
	const char *refname = updates[i].refname;

	if (i != 0)
		prefix_length = find_prefix(updates[i - 1].refname, refname);

	suffix_length = strlen(refname) - prefix_length;
	suffix_and_type = suffix_length << 3 | 0;

	/* 16 * 3 as there are 3 varints */
	max_full_length = 16 * 3 + suffix_length;

	/* Give up adding an index record if there might not be enough space */
	if (max_full_length > max_size)
		return 0;

	/* Actually add the ref record */
	pos += encode_varint(prefix_length, pos);
	pos += encode_varint(suffix_and_type, pos);
	pos += encode_data(refname + prefix_length, suffix_length, pos);
	pos += encode_varint(block_pos, pos);

	return pos - index_records;
}

uintmax_t get_block_pos(const struct ref_update *update)
{
	/* TODO: compute block_pos from update */

	return 0;
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
int reftable_add_ref_index(char *index_buf,
			   int index_count,
			   uintmax_t max_size,
			   uint32_t block_size,
			   const struct ref_update *updates,
			   int nr_updates)
{
	uint32_t block_start_len = 0, block_end_len = 0;
	int i, restart_count = 0;
	char *index_restarts;

	/*
	 * For now let's allocate index_restarts.
	 * TODO: reuse a block for ref_restarts, and/or:
	 * TODO: optimize size allocated for ref_restarts
	 */
	index_restarts = xcalloc(1, block_size);

	for (i = 0; i++; i < index_count) {
		uintmax_t block_pos = get_block_pos(&updates[i]);
		int record_len = reftable_add_index_record(index_buf, max_size, i,
							   updates, block_pos);

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
			block_end_len += encode_uint24nl(block_start_len, index_restarts + block_end_len);
			restart_count++;
		}


	}

}

/*
 * Add an object record to `object_records`.
 *
 * Size of `object_records` must be at least `max_size`.
 *
 * Return the size of the object record that could be added to
 * `object_records`. Return 0 if no record could be added because it
 * would be larger than `max_size`.
 *
 * Object record format:
 *
 *   varint( prefix_length )
 *   varint( (suffix_length << 3) | cnt_3 )
 *   suffix
 *   varint( cnt_large )?
 *   varint( position_delta )*
 *
 */
int reftable_add_object_record(char *object_records,
			      uintmax_t max_size,
			      int i,
			      const char **refnames,
			      uintmax_t block_pos)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t max_full_length;
	char *pos = object_records;

	if (i != 0)
		prefix_length = find_prefix(refnames[i - 1], refnames[i]);

	suffix_length = strlen(refnames[i]) - prefix_length;
	suffix_and_type = suffix_length << 3 | 0;

	/* 16 * 3 as there are 3 varints */
	max_full_length = 16 * 3 + suffix_length;

	/* Give up adding an object record if there might not be enough space */
	if (max_full_length > max_size)
		return 0;

	/* Actually add the ref record */
	pos += encode_varint(prefix_length, pos);
	pos += encode_varint(suffix_and_type, pos);
	pos += encode_data(refnames[i] + prefix_length, suffix_length, pos);
	pos += encode_varint(block_pos, pos);

	return pos - object_records;
}

int reftable_write_reftable_blocks(int fd, uint32_t block_size,
				   const struct ref_update *updates, int nr_updates)
{
	char *ref_records;
	unsigned int ref_written;
	struct reftable_header header;
	uint64_t min_update_index;
	uint64_t max_update_index;
	int padding = 1;

	/* Create ref header */
	reftable_header_init(&header, block_size,
			     min_update_index, max_update_index);

	/* Add ref records blocks */


	ref_records = xcalloc(1, block_size);

	/* Loop until all refs have been written */

	ref_written = reftable_add_ref_block(ref_records,
					     &header,
					     block_size,
					     padding,
					     updates,
					     nr_updates);
	reftable_write_data(fd, ref_records, block_size);


	return 0;
}
