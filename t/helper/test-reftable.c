#include "cache.h"
#include "test-tool.h"
#include "refs.h"
#include "refs/reftable.h"
#include "refs/refs-internal.h"


static const struct ref_update **updates;
static int nr_updates;
static int alloc_updates;

void register_update(struct ref_update *up)
{
	ALLOC_GROW(updates, nr_updates + 1, alloc_updates);
	updates[nr_updates++] = up;
}

/*
 * Put each ref into `updates`. 
 */
int get_all_refs(const char *refname, const struct object_id *oid,
		   int flags, void *cb_data)
{
	
	struct ref_update *update;

	FLEX_ALLOC_STR(update, refname, refname);

	oidcpy(&update.new_oid, oid); 

	register_update(update);
	
	return 0;
}

/*
 * Get refs from current repo and write them in a reftable file at the
 * given path.
 */
static int cmd_write_file(const char **argv)
{
	const char *path = *argv++;

	if (!path)
		die("file path required");

	setup_git_directory();

	refs_for_each_ref(get_all_refs);

	/*
	 * Write refs in a reftable file.
	 */
	
	return 0;
}

struct command {
	const char *name;
	int (*func)(const char **argv);
};

static struct command commands[] = {
	{ "write-file", cmd_write_file },
	{ NULL, NULL }
};

int cmd__reftable(int argc, const char **argv)
{
	const char *func;
	struct command *cmd;

	func = *argv++;
	if (!func)
		die("ref function required");
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcmp(func, cmd->name))
			return cmd->func(argv);
	}
	die("unknown function %s", func);
	return 0;
}
