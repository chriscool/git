#include "cache.h"
#include "dir.h"
#include "ewah/ewok.h"
#include "fsmonitor.h"
#include "run-command.h"
#include "strbuf.h"

/*
 * Mostly randomly chosen minimum index entry count.  We want enough entries
 * to be worth spawing the query-fsmonitor process.
 */
#define MINIMUM_ENTRIES			(50000)
#define INDEX_EXTENSION_VERSION	(1)
#define HOOK_INTERFACE_VERSION	(1)


int read_fsmonitor_extension(struct index_state *istate, const void *data,
	unsigned long sz)
{
	const char *index = data;
	uint32_t hdr_version;
	uint32_t ewah_size;
	int ret;

	if (sz < sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t))
		return error("corrupt fsmonitor extension (too short)");

	hdr_version = get_be32(index);
	index += sizeof(uint32_t);
	if (hdr_version != INDEX_EXTENSION_VERSION)
		return error("bad fsmonitor version %d", hdr_version);

	istate->fsmonitor_last_update = get_be64(index);
	index += sizeof(uint64_t);

	ewah_size = get_be32(index);
	index += sizeof(uint32_t);

	istate->fsmonitor_dirty = ewah_new();
	ret = ewah_read_mmap(istate->fsmonitor_dirty, index, ewah_size);
	if (ret != ewah_size) {
		ewah_free(istate->fsmonitor_dirty);
		istate->fsmonitor_dirty = NULL;
		return error("failed to parse ewah bitmap reading fsmonitor index extension");
	}

	return 0;
}

void write_fsmonitor_extension(struct strbuf *sb, struct index_state *istate)
{
	uint32_t hdr_version;
	uint64_t tm;
	struct ewah_bitmap *bitmap;
	int i;
	uint32_t ewah_start;
	uint32_t ewah_size = 0;
	int fixup = 0;

	put_be32(&hdr_version, INDEX_EXTENSION_VERSION);
	strbuf_add(sb, &hdr_version, sizeof(uint32_t));

	put_be64(&tm, istate->fsmonitor_last_update);
	strbuf_add(sb, &tm, sizeof(uint64_t));
	fixup = sb->len;
	strbuf_add(sb, &ewah_size, sizeof(uint32_t)); /* we'll fix this up later */

	ewah_start = sb->len;
	bitmap = ewah_new();
	for (i = 0; i < istate->cache_nr; i++)
		if (!(istate->cache[i]->ce_flags & CE_FSMONITOR_CLEAN))
			ewah_set(bitmap, i);
	ewah_serialize_strbuf(bitmap, sb);
	ewah_free(bitmap);

	/* fix up size field */
	put_be32(&ewah_size, sb->len - ewah_start);
	memcpy(sb->buf + fixup, &ewah_size, sizeof(uint32_t));
}

static struct untracked_cache_dir *find_untracked_cache_dir(
	struct untracked_cache *uc, struct untracked_cache_dir *ucd,
	const char *name)
{
	const char *end;
	struct untracked_cache_dir *dir = ucd;

	if (!*name)
		return dir;

	end = strchr(name, '/');
	if (end) {
		dir = lookup_untracked(uc, ucd, name, end - name);
		if (dir)
			return find_untracked_cache_dir(uc, dir, end + 1);
	}

	return dir;
}

/* This function will be passed to ewah_each_bit() */
static void mark_fsmonitor_dirty(size_t pos, void *is)
{
	struct index_state *istate = is;
	struct untracked_cache_dir *dir;
	struct cache_entry *ce = istate->cache[pos];

	assert(pos < istate->cache_nr);
	ce->ce_flags &= ~CE_FSMONITOR_CLEAN;

	if (!istate->untracked || !istate->untracked->root)
		return;

	dir = find_untracked_cache_dir(istate->untracked, istate->untracked->root, ce->name);
	if (dir)
		dir->valid = 0;
}

/*
 * Call the query-fsmonitor hook passing the time of the last saved results.
 */
static int query_fsmonitor(int version, uint64_t last_update, struct strbuf *query_result)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	char ver[64];
	char date[64];
	const char *argv[4];

	if (!(argv[0] = find_hook("query-fsmonitor")))
		return -1;

	snprintf(ver, sizeof(version), "%d", version);
	snprintf(date, sizeof(date), "%" PRIuMAX, (uintmax_t)last_update);
	argv[1] = ver;
	argv[2] = date;
	argv[3] = NULL;
	cp.argv = argv;
	cp.out = -1;

	return capture_command(&cp, query_result, 1024);
}

static void mark_file_dirty(struct index_state *istate, const char *name)
{
	struct untracked_cache_dir *dir;
	int pos;

	/* find it in the index and mark that entry as dirty */
	pos = index_name_pos(istate, name, strlen(name));
	if (pos >= 0) {
		if (istate->cache[pos]->ce_flags & CE_FSMONITOR_CLEAN) {
			istate->cache[pos]->ce_flags &= ~CE_FSMONITOR_CLEAN;
			istate->cache_changed |= FSMONITOR_CHANGED;
		}
	}

	/*
	 * Find the corresponding directory in the untracked cache
	 * and mark it as invalid
	 */
	if (!istate->untracked || !istate->untracked->root)
		return;

	dir = find_untracked_cache_dir(istate->untracked, istate->untracked->root, name);
	if (dir) {
		if (dir->valid) {
			dir->valid = 0;
			istate->cache_changed |= FSMONITOR_CHANGED;
		}
	}
}

static void refresh_by_fsmonitor(struct index_state *istate)
{
	struct strbuf query_result = STRBUF_INIT;
	int query_success = 0;
	size_t bol; /* beginning of line */
	uint64_t last_update;
	char *buf;
	int i;

	/*
	 * This could be racy so save the date/time now and query_fsmonitor
	 * should be inclusive to ensure we don't miss potential changes.
	 */
	last_update = getnanotime();

	/*
	 * If we have a last update time, call query_fsmonitor for the set of
	 * changes since that time.
	 */
	if (istate->fsmonitor_last_update && (istate->cache_nr >= MINIMUM_ENTRIES)) {
		query_success = !query_fsmonitor(HOOK_INTERFACE_VERSION,
			istate->fsmonitor_last_update, &query_result);
		trace_performance_since(last_update, "query-fsmonitor");
	}

	if (query_success) {
		/* Mark all entries clean */
		for (i = 0; i < istate->cache_nr; i++)
			istate->cache[i]->ce_flags |= CE_FSMONITOR_CLEAN;

		/* Mark all previously saved entries as dirty */
		if (istate->fsmonitor_dirty)
			ewah_each_bit(istate->fsmonitor_dirty, mark_fsmonitor_dirty, istate);

		/* Mark all entries returned by the monitor as dirty */
		buf = query_result.buf;
		bol = 0;
		for (i = 0; i < query_result.len; i++) {
			if (buf[i] != '\0')
				continue;
			mark_file_dirty(istate, buf + bol);
			bol = i + 1;
		}
		if (bol < query_result.len)
			mark_file_dirty(istate, buf + bol);

		/*
		 * Now that we've marked the invalid entries in the
		 * untracked-cache itself, we can mark the untracked cache for
		 * fsmonitor usage.
		 */
		if (istate->untracked)
			istate->untracked->use_fsmonitor = 1;
	}
	strbuf_release(&query_result);

	/* Now that we've updated istate, save the last_update time */
	istate->fsmonitor_last_update = last_update;
}

void tweak_fsmonitor_extension(struct index_state *istate)
{
	int val, i;

	if (!git_config_get_maybe_bool("core.fsmonitor", &val))
		core_fsmonitor = val;

	if (core_fsmonitor) {
		if (!istate->fsmonitor_last_update)
			istate->cache_changed |= FSMONITOR_CHANGED;

		refresh_by_fsmonitor(istate);
	} else {
		if (istate->fsmonitor_last_update)
			istate->cache_changed |= FSMONITOR_CHANGED;
		istate->fsmonitor_last_update = 0;
	}

	if (istate->fsmonitor_dirty) {
		ewah_free(istate->fsmonitor_dirty);
		istate->fsmonitor_dirty = NULL;
	}
}
