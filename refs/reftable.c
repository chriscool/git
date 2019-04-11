#include "cache.h"
#include "refs.h"
#include "refs/refs-internal.h"
#include "refs/reftable.h"
#include "refs/ref-update-array.h"
#include "varint.h"

#define REFTABLE_SIGNATURE 0x52454654  /* "REFT" */
#define REFTABLE_VERSION 1

struct reftable_header {
	unsigned int signature: 32;
	unsigned int version_number: 8;
	unsigned int block_size: 24;
	uint64_t min_update_index;
	uint64_t max_update_index;
};

/*
 * The way reftable is written has been copied from the way cache
 * entries are written.
 *
 * See ce_write_flush() and ce_write() in read-cache.c.
 */

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

static int reftable_read_data(int fd, void *data,
			      unsigned int len, off_t offset)
{
	/*
	 * TODO: use mmap if possible
	 */
	ssize_t bytes_read = pread_in_full(fd, data, len, offset);
	if (bytes_read < 0 || bytes_read != len)
		return -1;
	return 0;
}

static void reftable_header_init(struct reftable_header *header, uint32_t block_size,
				 uint64_t min_update_index, uint64_t max_update_index)
{
	header->signature = htonl(REFTABLE_SIGNATURE);
	header->version_number = REFTABLE_VERSION;

	if (block_size > 0xffffff)
		BUG("too big block size '%d'", block_size);
	header->block_size = htonl(block_size);

	header->min_update_index = htonl(min_update_index);
	header->max_update_index = htonl(max_update_index);
}

static void reftable_header_check(struct reftable_header *header)
{
	uint32_t signature = ntohl(header->signature);
	uint8_t version = header->version_number;

	if (signature != REFTABLE_SIGNATURE)
		BUG("invalid reftable signature '%d' instead of '%d'", signature, REFTABLE_SIGNATURE);
	if (version != REFTABLE_VERSION)
		BUG("invalid reftable version '%d' instead of '%d'", version, REFTABLE_VERSION);
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

static size_t decode_data(void *dst, size_t n, const void *buf)
{
	memcpy(dst, buf, n);
	return n;
}

static void encode_padding(size_t n, void *buf)
{
	memset(buf, 0, n);
}

static size_t encode_uint16nl(uint16_t val, void *buf)
{
	uint16_t nl_val = htons(val);
	const char *p = (char *)&nl_val;

	return encode_data(p, 2, buf);
}

static size_t decode_uint16nl(uint16_t *val, const void *buf)
{
	uint16_t nl_val;
	char *p = (char *)&nl_val;
	size_t res = decode_data(p, 2, buf);

	*val = ntohs(nl_val);

	return res;
}

static size_t encode_uint24nl(uint32_t val, void *buf)
{
	uint32_t nl_val = htonl(val);
	const char *p = (char *)&nl_val;

	if (val >> 24)
		BUG("too big value '%d' for uint24", val);

	return encode_data(p + 1, 3, buf);
}

static size_t decode_uint24nl(uint32_t *val, const void *buf)
{
	uint32_t nl_val = 0;
	char *p = (char *)&nl_val;
	size_t res = decode_data(p + 1, 3, buf);

	*val = ntohl(nl_val);

	return res;
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

static size_t decode_reftable_header(struct reftable_header *header, const void *buf)
{
	const int header_size =  sizeof(*header);

	if (header_size != 24)
		BUG("bad reftable header size '%d' instead of 24", header_size);

	return decode_data(header, header_size, buf);
}

/*
 * Add a restart into a ref block at most after this number of refs.
 */
const int reftable_restart_gap = 16;

/* Compute max_value_length */
uintmax_t get_max_value(int value_type, const struct ref_update *update,
			const unsigned char **refvalue, uintmax_t *target_length)
{
	switch (value_type) {
	case 0x0:
		return 0;
	case 0x1:
		*refvalue = update->new_oid.hash;
		return the_hash_algo->rawsz;
	case 0x2:
		*refvalue = update->new_oid.hash;
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
int reftable_add_ref_record(unsigned char *ref_records,
			    uintmax_t max_size,
			    int i,
			    struct ref_update_array *update_array,
			    uintmax_t update_index_delta,
			    int restart)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t target_length = 0;
	uintmax_t max_value_length;
	uintmax_t max_full_length;
	unsigned char *pos = ref_records;
	const char *refname = update_array->updates[i]->refname;
	const unsigned char *refvalue = NULL;
	int value_type;
	struct object_id peeled;

	if (i == 0 && !restart)
		BUG("first ref record is always a restart");

	value_type = get_value_type(update_array->updates[i], &peeled);
	if (value_type < 0)
		return value_type;

	if (!restart)
		prefix_length = find_prefix(update_array->updates[i - 1]->refname, refname);

	suffix_length = strlen(refname) - prefix_length;
	suffix_and_type = suffix_length << 3 | value_type;

	max_value_length = get_max_value(value_type, update_array->updates[i],
					 &refvalue, &target_length);

	if (value_type && !refvalue)
		BUG("couldn't find value for ref '%s'", refname);

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
		pos += encode_data(refvalue, the_hash_algo->rawsz, pos);
		pos += encode_data(peeled.hash, the_hash_algo->rawsz, pos);
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
 * Add a ref block to `ref_records`.
 *
 * The refs added to the block are taken from `update_array`.
 *
 * Return the index of the first ref in `update_array` that could NOT
 * be added into the ref block. (If it is equal to the number of refs
 * in `update_array`, then it means all the refs have been added.)
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
static int reftable_add_ref_block(unsigned char *ref_records,
				  struct reftable_header *header,
				  uint32_t block_size,
				  int padding,
				  struct ref_update_array *update_array,
				  int start_ref)
{
	uint32_t block_start_len = 0, block_end_len = 0;
	uint32_t restart_offset = 0;
	int i, nb_refs = 0, restart_count = 0;
	char *ref_restarts;
	unsigned char *block_len_pos;

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

	/* Add 'r' */
	block_start_len += encode_data("r", 1, ref_records + block_start_len);

	/* We don't know the block_len, so we postpone adding uint24( block_len ) */
	block_len_pos = ref_records + block_start_len;
	block_start_len += 3;

	/* Add first restart offset */
	block_end_len += encode_uint24nl(block_start_len, ref_restarts + block_end_len);
	restart_count++;

	for (i = start_ref; i < update_array->nr; i++) {
		int restart = ((i % reftable_restart_gap) == 0);

		int max_size = block_size - (block_start_len + block_end_len + 2);
		uintmax_t update_index_delta = get_update_index_delta(update_array->updates[i]);
		int record_len = reftable_add_ref_record(ref_records + block_start_len, max_size,
							 i, update_array, update_index_delta, restart);

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
int reftable_add_index_record(unsigned char *index_records,
			      uintmax_t max_size,
			      int i,
			      struct ref_update_array *update_array,
			      uintmax_t block_pos,
			      int restart)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t max_full_length;
	unsigned char *pos = index_records;
	const char *refname = update_array->updates[i]->refname;

	if (i != 0)
		prefix_length = find_prefix(update_array->updates[i - 1]->refname, refname);

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
 * Add an index block to `index_records`.
 *
 * The refs added to the block are taken from `update_array`.
 *
 * Return the index of the first ref in `update_array` that could NOT
 * be added into the index block. (If it is equal to the number of
 * refs in `update_array`, then it means all the refs have been
 * added.)
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
int reftable_add_index_block(unsigned char *index_records,
			     uint32_t block_size,
			     struct ref_update_array *update_array,
			     int start_index)
{
	uint32_t block_start_len = 0, block_end_len = 0;
	int i, restart_count = 0;
	char *index_restarts;
	unsigned char *block_len_pos;

	if (block_size < 2000)
		BUG("too small reftable index size '%d'", block_size);

	/*
	 * For now let's allocate index_restarts.
	 * TODO: reuse a block for index_restarts, and/or:
	 * TODO: optimize size allocated for index_restarts
	 */
	index_restarts = xcalloc(1, block_size);

	/* Add 'i' */
	block_start_len += encode_data("i", 1, index_records + block_start_len);

	/* We don't know the block_len, so we postpone adding uint24( block_len ) */
	block_len_pos = index_records + block_start_len;
	block_start_len += 3;

	/* Add first restart offset */
	block_end_len += encode_uint24nl(block_start_len, index_restarts + block_end_len);
	restart_count++;

	for (i = start_index; i < update_array->nr; i++) {
		int restart = ((i % reftable_restart_gap) == 0);

		int max_size = block_size - (block_start_len + block_end_len + 2);
		uintmax_t block_pos = get_block_pos(update_array->updates[i]);
		int record_len = reftable_add_index_record(index_records, max_size, i,
							   update_array, block_pos,
							   restart);

		if (record_len < 1)
			break;

		/* Add the record */
		block_start_len += record_len;

		/*
		 * Add a restart after reftable_restart_gap ref
		 * records if there is some space left in the block.
		 */
		if (restart && block_size - (block_start_len + block_end_len + 2) > 3) {
			block_end_len += encode_uint24nl(block_start_len, index_restarts + block_end_len);
			restart_count++;
		}
	}

	/* Add restart count */
	block_end_len += encode_uint16nl(restart_count, index_restarts + block_end_len);

	/* Copy restarts into the records block */
	block_start_len += encode_data(index_restarts, block_end_len, index_records + block_start_len);

	free(index_restarts);

	/* Write block_len at the beginning of the block */
	encode_uint24nl(block_start_len, block_len_pos);

	/* Add padding */
	encode_padding(block_size - block_start_len, index_records + block_start_len);

	return i;
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
int reftable_add_object_record(unsigned char *object_records,
			      uintmax_t max_size,
			      int i,
			      const char **refnames,
			      uintmax_t block_pos)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	uintmax_t max_full_length;
	unsigned char *pos = object_records;

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

int reftable_write_reftable_blocks(int fd, uint32_t block_size, const char *path,
				   struct ref_update_array *update_array,
				   int padding)
{
	unsigned char *ref_records;
	unsigned int ref_written = 0;
	struct reftable_header header;
	uint64_t min_update_index;
	uint64_t max_update_index;

	/* Create ref header */
	reftable_header_init(&header, block_size,
			     min_update_index, max_update_index);

	/* Add ref records blocks */

	ref_records = xcalloc(1, block_size);

	while (ref_written < update_array->nr) {
		ref_written = reftable_add_ref_block(ref_records,
						     ref_written == 0 ? &header : NULL,
						     block_size,
						     padding,
						     update_array,
						     ref_written);
		if (reftable_write_data(fd, ref_records, block_size))
			die_errno("couldn't write to '%s'", path);
	}

	free(ref_records);

	return 0;
}

/*
 * Read a ref record from `ref_records`.
 *
 * Nothing should be read at or after `max_ref_record_pos`.
 *
 * Return the size of the ref record that was read.
 * Return 0 if no record could be read because it
 * would read at or after `max_ref_record_pos`.
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
static int reftable_read_ref_record(unsigned char *ref_records,
				    uint32_t current_restart,
				    const unsigned char *max_ref_record_pos,
				    struct ref_update_array *update_array,
				    uintmax_t *update_index_delta)
{
	uintmax_t prefix_length = 0;
	uintmax_t suffix_length;
	uintmax_t suffix_and_type;
	const unsigned char *pos = ref_records;
	int value_type;
	struct ref_update *update;
	char *refname;

	/* TODO: return an error instead of BUG()ing */
	if (update_array->nr == 0 && !current_restart)
		BUG("reftable_read_ref_record: first ref record is always a restart");

	/* Read varint( prefix_length ) */
	prefix_length = decode_varint(&pos);
	if (pos >= max_ref_record_pos)
		return 0;

	/* Read varint( (suffix_length << 3) | value_type ) */
	suffix_and_type = decode_varint(&pos);
	if (pos >= max_ref_record_pos)
		return 0;

	value_type = suffix_and_type & 0x7;
	suffix_length = suffix_and_type >> 3;

	refname = xmallocz(prefix_length + suffix_length);

	if (prefix_length) {
		const char *previous = update_array->updates[update_array->nr - 1]->refname;

		/* TODO: return an error instead of BUG()ing */
		if (strlen(previous) < prefix_length)
			BUG("reftable_read_ref_record: previous lenght too big");

		memcpy(refname, previous, prefix_length);
	}

	/* TODO free `refname` in case of error */
	/* Read suffix */
	pos += decode_data(refname + prefix_length, suffix_length, pos);
	if (pos >= max_ref_record_pos)
		return 0;

	FLEX_ALLOC_STR(update, refname, refname);
	free(refname);

	/* TODO free `update` in case of error */
	/* Read varint( update_index_delta ) */
	*update_index_delta = decode_varint(&pos);
	if (pos >= max_ref_record_pos)
		return 0;

	/* Read value? */
	switch (value_type) {
	case 0x0:
		break;
	case 0x1:
		pos += decode_data(&update->new_oid, the_hash_algo->rawsz, pos);
		update->flags |= REF_HAVE_NEW;
		break;
	case 0x2:
		pos += decode_data(&update->new_oid, the_hash_algo->rawsz, pos);
		update->flags |= REF_HAVE_NEW;
		/* Peeled data goes into backend_data */
		update->backend_data = xmalloc(the_hash_algo->rawsz);
		pos += decode_data(update->backend_data, the_hash_algo->rawsz, pos);
		update->flags |= REF_KNOWS_PEELED;
		break;
	case 0x3:
		/* TODO:
		pos += decode_varint(target_length, pos);
		pos += decode_data(refvalue, target_length, pos);
		*/
		break;
	default:
		/* TODO: return an error instead of BUG()ing */
		BUG("unknown value_type '%d'", value_type);
	}

	ref_update_array_append(update_array, update);

	return pos - ref_records;
}

static uint32_t get_current_restart_offset(uint32_t block_start_len, uint32_t *restart_offsets,
					   int *restart_index, uint16_t restart_count)
{
	while (*restart_index < restart_count && restart_offsets[*restart_index] < block_start_len)
		*restart_index++;

	if (*restart_index >= restart_count || restart_offsets[*restart_index] > block_start_len)
		return 0;

	/* restart_offsets[*restart_index] == block_start_len */
	*restart_index++;
	return block_start_len;
}

static void print_restart_offsets(uint32_t *restart_offsets, uint16_t restart_count)
{
	uint16_t i;

	fprintf(stderr, "restart count: %d\n", restart_count);
	for (i = 0; i < restart_count; i++)
		fprintf(stderr, "restart[%d]=%d\n", i, restart_offsets[i]);
	fprintf(stderr, "done\n");
}

/*
 * Read a ref block from `ref_records`.
 *
 * The refs read from the block are written into `updates`.
 *
 * Return the number of refs that could be read from the ref block.
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
static int reftable_read_ref_block(unsigned char *ref_records,
				   struct reftable_header *header,
				   uint32_t block_size,
				   int padding,
				   struct ref_update_array *update_array)
{
	int i;
	uint32_t block_start_len = 0;
	uint32_t block_end_len = 0;
	char r;
	uint32_t block_len;
	uint16_t restart_count = 0;
	uint32_t *restart_offsets;
	int restart_index = 0;
	const unsigned char *max_ref_record_pos;

	/* Read header */
	if (header) {
		block_start_len += decode_reftable_header(header, ref_records + block_start_len);
		reftable_header_check(header);
	}

	/* Read 'r' */
	block_start_len += decode_data((void *)&r, 1, ref_records + block_start_len);
	if (r != 'r')
		return error(_("error reading 'r' in ref block"));

	/* Read uint24( block_len ) */
	block_start_len += decode_uint24nl(&block_len, ref_records + block_start_len);

	/* Read uint16( restart_count ) from the end */
	block_end_len += decode_uint16nl(&restart_count, ref_records + block_len - 2);

	/* Read uint24( restart_offset )+ from the end */
	restart_offsets = xcalloc(restart_count, sizeof(*restart_offsets));
	for (i = 0; i < restart_count; i++) {
		char *pos = ref_records + block_len - 2 - (restart_count - i) * 3;
		decode_uint24nl(&restart_offsets[i], pos);
	}

	/* Read ref_record+ */

	max_ref_record_pos = ref_records + block_len - 2 - restart_count * 3;

	while (block_start_len < block_len - block_end_len) {
		uintmax_t update_index_delta;
		uint32_t current_restart = get_current_restart_offset(block_start_len, restart_offsets,
								      &restart_index, restart_count);
		int record_len = reftable_read_ref_record(ref_records + block_start_len,
							  current_restart, max_ref_record_pos,
							  update_array, &update_index_delta);

		if (record_len < 1)
			break;

		/* Add the record */
		block_start_len += record_len;
	}

	return update_array->nr;
}

int reftable_read_reftable_blocks(int fd, uint32_t block_size, const char *path,
				  struct ref_update_array *update_array)
{
	unsigned int ref_read = 0;
	struct reftable_header header;
	unsigned char *ref_records = xcalloc(1, block_size);
	int padding = 1; /* TODO: move this up the call chain */
	off_t offset = 0;

	if (reftable_read_data(fd, ref_records, block_size, offset))
		die_errno("couldn't read from '%s'", path);

	/* TODO: find how many blocks we should read */

	while (1) {
		ref_read = reftable_read_ref_block(ref_records,
						   ref_read == 0 ? &header : NULL,
						   block_size,
						   padding,
						   update_array);
		offset += block_size;
		if (reftable_read_data(fd, ref_records, block_size, offset))
			break;
	}

	return 0;
}
