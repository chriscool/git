#include "cache.h"
#include "attr.h"
#include "object.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "delta.h"
#include "pack.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "progress.h"
#include "refs.h"
#include "khash.h"
#include "pack-bitmap.h"
#include "pack-objects.h"
#include "delta-islands.h"
#include "sha1-array.h"
#include "config.h"

KHASH_INIT(str, const char *, void *, 1, kh_str_hash_func, kh_str_hash_equal)

static khash_sha1 *island_marks;
static unsigned island_counter;
static unsigned island_counter_core;

static kh_str_t *remote_islands;

struct remote_island {
	uint64_t hash;
	struct oid_array oids;
};

struct island_bitmap {
	uint32_t refcount;
	uint32_t bits[];
};

static uint32_t island_bitmap_size;

/*
 * Allocate a new bitmap; if "old" is not NULL, the new bitmap will be a copy
 * of "old". Otherwise, the new bitmap is empty.
 */
static struct island_bitmap *island_bitmap_new(const struct island_bitmap *old)
{
	size_t size = sizeof(struct island_bitmap) + (island_bitmap_size * 4);
	struct island_bitmap *b = xcalloc(1, size);

	if (old)
		memcpy(b, old, size);

	b->refcount = 1;
	return b;
}

static void island_bitmap_or(struct island_bitmap *a, const struct island_bitmap *b)
{
	uint32_t i;

	for (i = 0; i < island_bitmap_size; ++i)
		a->bits[i] |= b->bits[i];
}

static int island_bitmap_is_subset(struct island_bitmap *self,
		struct island_bitmap *super)
{
	uint32_t i;

	if (self == super)
		return 1;

	for (i = 0; i < island_bitmap_size; ++i) {
		if ((self->bits[i] & super->bits[i]) != self->bits[i])
			return 0;
	}

	return 1;
}

#define ISLAND_BITMAP_BLOCK(x) (x / 32)
#define ISLAND_BITMAP_MASK(x) (1 << (x % 32))

static void island_bitmap_set(struct island_bitmap *self, uint32_t i)
{
	self->bits[ISLAND_BITMAP_BLOCK(i)] |= ISLAND_BITMAP_MASK(i);
}

static int island_bitmap_get(struct island_bitmap *self, uint32_t i)
{
	return (self->bits[ISLAND_BITMAP_BLOCK(i)] & ISLAND_BITMAP_MASK(i)) != 0;
}

int in_same_island(const struct object_id *trg_oid, const struct object_id *src_oid)
{
	khiter_t trg_pos, src_pos;

	/* If we aren't using islands, assume everything goes together. */
	if (!island_marks)
		return 1;

	/*
	 * If we don't have a bitmap for the target, we can delta it
	 * against anything -- it's not an important object
	 */
	trg_pos = kh_get_sha1(island_marks, trg_oid->hash);
	if (trg_pos >= kh_end(island_marks))
		return 1;

	/*
	 * if the source (our delta base) doesn't have a bitmap,
	 * we don't want to base any deltas on it!
	 */
	src_pos = kh_get_sha1(island_marks, src_oid->hash);
	if (src_pos >= kh_end(island_marks))
		return 0;

	return island_bitmap_is_subset(kh_value(island_marks, trg_pos),
				kh_value(island_marks, src_pos));
}

int island_delta_cmp(const struct object_id *a, const struct object_id *b)
{
	khiter_t a_pos, b_pos;
	struct island_bitmap *a_bitmap = NULL, *b_bitmap = NULL;

	if (!island_marks)
		return 0;

	a_pos = kh_get_sha1(island_marks, a->hash);
	if (a_pos < kh_end(island_marks))
		a_bitmap = kh_value(island_marks, a_pos);

	b_pos = kh_get_sha1(island_marks, b->hash);
	if (b_pos < kh_end(island_marks))
		b_bitmap = kh_value(island_marks, b_pos);

	if (a_bitmap) {
		if (!b_bitmap || !island_bitmap_is_subset(a_bitmap, b_bitmap))
			return -1;
	}
	if (b_bitmap) {
		if (!a_bitmap || !island_bitmap_is_subset(b_bitmap, a_bitmap))
			return 1;
	}

	return 0;
}

static struct island_bitmap *create_or_get_island_marks(struct object *obj)
{
	khiter_t pos;
	int hash_ret;

	pos = kh_put_sha1(island_marks, obj->oid.hash, &hash_ret);
	if (hash_ret)
		kh_value(island_marks, pos) = island_bitmap_new(NULL);

	return kh_value(island_marks, pos);
}

static void set_island_marks(struct object *obj, struct island_bitmap *marks)
{
	struct island_bitmap *b;
	khiter_t pos;
	int hash_ret;

	pos = kh_put_sha1(island_marks, obj->oid.hash, &hash_ret);
	if (hash_ret) {
		/*
		 * We don't have one yet; make a copy-on-write of the
		 * parent.
		 */
		marks->refcount++;
		kh_value(island_marks, pos) = marks;
		return;
	}

	/*
	 * We do have it. Make sure we split any copy-on-write before
	 * updating.
	 */
	b = kh_value(island_marks, pos);
	if (b->refcount > 1) {
		b->refcount--;
		b = kh_value(island_marks, pos) = island_bitmap_new(b);
	}
	island_bitmap_or(b, marks);
}

static void mark_remote_island_1(struct remote_island *rl, int is_core_island)
{
	uint32_t i;

	for (i = 0; i < rl->oids.nr; ++i) {
		struct island_bitmap *marks;
		struct object *obj = parse_object(&rl->oids.oid[i]);

		if (!obj)
			continue;

		marks = create_or_get_island_marks(obj);
		island_bitmap_set(marks, island_counter);

		if (is_core_island && obj->type == OBJ_COMMIT)
			obj->flags |= NEEDS_BITMAP;

		/* If it was a tag, also make sure we hit the underlying object. */
		while (obj && obj->type == OBJ_TAG) {
			obj = ((struct tag *)obj)->tagged;
			if (obj) {
				parse_object(&obj->oid);
				marks = create_or_get_island_marks(obj);
				island_bitmap_set(marks, island_counter);
			}
		}
	}

	if (is_core_island)
		island_counter_core = island_counter;

	island_counter++;
}

static int cmp_tree_depth(const void *va, const void *vb)
{
	struct object_entry *a = *(struct object_entry **)va;
	struct object_entry *b = *(struct object_entry **)vb;
	return a->tree_depth - b->tree_depth;
}

void resolve_tree_islands(int progress, struct packing_data *to_pack)
{
	struct progress *progress_state = NULL;
	struct object_entry **todo;
	int nr = 0;
	int i;

	if (!island_marks)
		return;

	/*
	 * We process only trees, as commits and tags have already been handled
	 * (and passed their marks on to root trees, as well. We must make sure
	 * to process them in descending tree-depth order so that marks
	 * propagate down the tree properly, even if a sub-tree is found in
	 * multiple parent trees.
	 */
	todo = xmalloc(to_pack->nr_objects * sizeof(*todo));
	for (i = 0; i < to_pack->nr_objects; i++) {
		if (oe_type(&to_pack->objects[i]) == OBJ_TREE)
			todo[nr++] = &to_pack->objects[i];
	}
	qsort(todo, nr, sizeof(*todo), cmp_tree_depth);

	if (progress)
		progress_state = start_progress(_("Propagating island marks"), nr);

	for (i = 0; i < nr; i++) {
		struct object_entry *ent = todo[i];
		struct island_bitmap *root_marks;
		struct tree *tree;
		struct tree_desc desc;
		struct name_entry entry;
		khiter_t pos;

		pos = kh_get_sha1(island_marks, ent->idx.oid.hash);
		if (pos >= kh_end(island_marks))
			continue;

		root_marks = kh_value(island_marks, pos);

		tree = lookup_tree(&ent->idx.oid);
		if (!tree || parse_tree(tree) < 0)
			die(_("bad tree object %s"), oid_to_hex(&ent->idx.oid));

		init_tree_desc(&desc, tree->buffer, tree->size);
		while (tree_entry(&desc, &entry)) {
			struct object *obj;

			if (S_ISGITLINK(entry.mode))
				continue;

			obj = lookup_object(entry.oid->hash);
			if (!obj)
				continue;

			set_island_marks(obj, root_marks);
		}

		free(tree->buffer);
		tree->buffer = NULL;
		tree->object.parsed = 0;

		display_progress(progress_state, i+1);
	}

	stop_progress(&progress_state);
	free(todo);
}

static regex_t *island_regexes;
static unsigned int island_regexes_alloc, island_regexes_nr;
static const char *core_island_name;

static int island_config_callback(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "pack.island")) {
		struct strbuf re = STRBUF_INIT;

		if (!v)
			return config_error_nonbool(k);

		if (island_regexes_nr >= island_regexes_alloc) {
			island_regexes_alloc = (island_regexes_alloc + 8) * 2;
			island_regexes = xrealloc(island_regexes,
					island_regexes_alloc * sizeof(regex_t));
		}

		if (*v != '^')
			strbuf_addch(&re, '^');
		strbuf_addstr(&re, v);

		if (regcomp(&island_regexes[island_regexes_nr], re.buf, REG_EXTENDED))
			die(_("failed to load island regex for '%s': %s"), k, re.buf);

		strbuf_release(&re);
		island_regexes_nr++;
		return 0;
	}

	if (!strcmp(k, "pack.islandcore"))
		return git_config_string(&core_island_name, k, v);

	return 0;
}

static void add_ref_to_island(const char *island_name, const struct object_id *oid)
{
	uint64_t sha_core;
	struct remote_island *rl = NULL;

	int hash_ret;
	khiter_t pos = kh_put_str(remote_islands, island_name, &hash_ret);

	if (hash_ret) {
		kh_key(remote_islands, pos) = xstrdup(island_name);
		kh_value(remote_islands, pos) = xcalloc(1, sizeof(struct remote_island));
	}

	rl = kh_value(remote_islands, pos);
	oid_array_append(&rl->oids, oid);

	memcpy(&sha_core, oid->hash, sizeof(uint64_t));
	rl->hash += sha_core;
}

static int find_island_for_ref(const char *refname, const struct object_id *oid,
			       int flags, void *data)
{
	int i, m;
	regmatch_t matches[8];
	struct strbuf island_name = STRBUF_INIT;

	/* walk backwards to get last-one-wins ordering */
	for (i = island_regexes_nr - 1; i >= 0; i--) {
		if (!regexec(&island_regexes[i], refname,
			     ARRAY_SIZE(matches), matches, 0))
			break;
	}

	if (i < 0)
		return 0;

	for (m = 1; m < ARRAY_SIZE(matches); m++) {
		regmatch_t *match = &matches[m];

		if (match->rm_so == -1)
			continue;

		if (island_name.len)
			strbuf_addch(&island_name, '-');

		strbuf_add(&island_name, refname + match->rm_so, match->rm_eo - match->rm_so);
	}

	add_ref_to_island(island_name.buf, oid);
	strbuf_release(&island_name);
	return 0;
}

static struct remote_island *get_core_island(void)
{
	if (core_island_name) {
		khiter_t pos = kh_get_str(remote_islands, core_island_name);
		if (pos < kh_end(remote_islands))
			return kh_value(remote_islands, pos);
	}

	return NULL;
}

static void deduplicate_islands(void)
{
	struct remote_island *island, *core = NULL, **list;
	unsigned int island_count, dst, src, ref, i = 0;

	island_count = kh_size(remote_islands);
	list = xmalloc(island_count * sizeof(struct remote_island *));

	kh_foreach_value(remote_islands, island, {
		list[i++] = island;
	});

	for (ref = 0; ref + 1 < island_count; ref++) {
		for (src = ref + 1, dst = src; src < island_count; src++) {
			if (list[ref]->hash == list[src]->hash)
				continue;

			if (src != dst)
				list[dst] = list[src];

			dst++;
		}
		island_count = dst;
	}

	island_bitmap_size = (island_count / 32) + 1;
	core = get_core_island();

	for (i = 0; i < island_count; ++i) {
		mark_remote_island_1(list[i], core && list[i]->hash == core->hash);
	}

	free(list);
}

void load_delta_islands(void)
{
	island_marks = kh_init_sha1();
	remote_islands = kh_init_str();

	git_config(island_config_callback, NULL);
	for_each_ref(find_island_for_ref, NULL);
	deduplicate_islands();

	fprintf(stderr, _("Marked %d islands, done.\n"), island_counter);
}

void propagate_island_marks(struct commit *commit)
{
	khiter_t pos = kh_get_sha1(island_marks, commit->object.oid.hash);

	if (pos < kh_end(island_marks)) {
		struct commit_list *p;
		struct island_bitmap *root_marks = kh_value(island_marks, pos);

		parse_commit(commit);
		set_island_marks(&get_commit_tree(commit)->object, root_marks);
		for (p = commit->parents; p; p = p->next)
			set_island_marks(&p->item->object, root_marks);
	}
}

int compute_pack_layers(struct packing_data *to_pack)
{
	uint32_t i;

	if (!core_island_name || !island_marks)
		return 1;

	for (i = 0; i < to_pack->nr_objects; ++i) {
		struct object_entry *entry = &to_pack->objects[i];
		khiter_t pos = kh_get_sha1(island_marks, entry->idx.oid.hash);

		entry->layer = 1;

		if (pos < kh_end(island_marks)) {
			struct island_bitmap *bitmap = kh_value(island_marks, pos);

			if (island_bitmap_get(bitmap, island_counter_core))
				entry->layer = 0;
		}
	}

	return 2;
}
