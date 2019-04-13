#include "cache.h"
#include "test-tool.h"
#include "refs.h"
#include "refs/reftable.h"
#include "refs/refs-internal.h"
#include "refs/ref-update-array.h"

/*
 * Put each ref into `updates`.
 */
static int get_all_refs(const char *refname, const struct object_id *oid,
			int flags, void *cb_data)
{
	struct ref_update *update;
	struct ref_update_array *update_array = (struct ref_update_array *)cb_data;

	FLEX_ALLOC_STR(update, refname, refname);

	oidcpy(&update->new_oid, oid);
	update->flags |= REF_HAVE_NEW;

	ref_update_array_append(update_array, update);

	return 0;
}

/*
 * Get refs from current repo and write them in a reftable file at the
 * given path.
 */
static int cmd_write_file(const char **argv)
{
	const char *path = *argv++;
	int fd;
	int res;
	uint32_t block_size = 1024 * 16; /* 16KB */
	int padding = 1; /* TODO: add a cli flag? */

	struct ref_update_array update_array = REF_UPDATE_ARRAY_INIT;

	if (!path)
		die("file path required");

	setup_git_directory();

	refs_for_each_ref(get_main_ref_store(the_repository), get_all_refs, &update_array);

	printf("nr updates: %ld\n", update_array.nr);

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0) {
		perror(path);
		return 1;
	}

	res = reftable_write_reftable_blocks(fd, block_size, path, &update_array, padding);

	close(fd);

	return res;
}

/*
 * Read refs from a reftable file at the given path.
 */
static int cmd_read_file(const char **argv)
{
	const char *path = *argv++;
	int fd, res, i;
	uint32_t block_size = 1024 * 16; /* 16KB */
	struct ref_update_array update_array = REF_UPDATE_ARRAY_INIT;

	if (!path)
		die("file path required");

	setup_git_directory();

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return 1;
	}

	res = reftable_read_reftable_blocks(fd, block_size, path, &update_array);

	close(fd);

	/* Print refs */
	for (i = 0; i < update_array.nr; i++) {
		struct ref_update *update = update_array.updates[i];
		printf("%s", update->refname);
		if (update->flags & REF_HAVE_NEW)
			printf(" %s", oid_to_hex(&update->new_oid));
		if (update->flags & REF_KNOWS_PEELED)
			printf(" %s", oid_to_hex(update->backend_data));
		printf("\n");
	}

	return res;
}

struct command {
	const char *name;
	int (*func)(const char **argv);
};

static struct command commands[] = {
	{ "write-reftable", cmd_write_file },
	{ "read-reftable", cmd_read_file },
	{ NULL, NULL }
};

int cmd__reftable(int argc, const char **argv)
{
	const char *func;
	struct command *cmd;

	/* Skip "reftable" */
	argv++;

	func = *argv++;
	if (!func)
		die("reftable function required");
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcmp(func, cmd->name))
			return cmd->func(argv);
	}
	die("unknown function %s", func);

	return 0;
}
