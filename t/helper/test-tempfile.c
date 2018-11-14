#include "cache.h"
#include "test-tool.h"
#include "tempfile.h"


static int cmd_create(const char **argv)
{
	const char *path = *argv++;
	struct tempfile *temp;
	struct stat st;

	if (path)
		die("'create' command supports no argument");

	setup_git_directory();

	temp = create_tempfile(git_path("sharedindex_XXXXXX"));

	if (close_tempfile_gently(temp))
		die("could not close '%s'", temp->filename.buf);

	if (stat(temp->filename.buf, &st))
		die("could not stat '%s'", temp->filename.buf);
}

static int cmd_mks(const char **argv)
{
	const char *path = *argv++;
	struct tempfile *temp;
	struct stat st;

	if (path)
		die("'mks' command supports no argument");

	setup_git_directory();

	temp = mks_tempfile(git_path("sharedindex_XXXXXX"));

	if (close_tempfile_gently(temp))
		die("could not close '%s'", temp->filename.buf);

	if (stat(temp->filename.buf, &st))
		die("could not stat '%s'", temp->filename.buf);
}

struct command {
	const char *name;
	int (*func)(const char **argv);
};

static struct command commands[] = {
	{ "create", cmd_create },
	{ "mks", cmd_mks },
	{ NULL, NULL }
};

int cmd__tempfile(int argc, const char **argv)
{
	const char *func;
	struct command *cmd;

	func = *argv++;
	if (!func)
		die("tempfile function required");
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcmp(func, cmd->name))
			return cmd->func(argv);
	}
	die("unknown function %s", func);
	return 0;
}
