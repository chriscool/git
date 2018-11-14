#include "cache.h"
#include "test-tool.h"
#include "tempfile.h"


static int cmd_tempfile(const char **argv)
{
	const char *path = *argv++;
	struct tempfile *temp;
	struct stat st;

	if (!path)
		die("tempfile command requires exactly one argument");

	setup_git_directory();

	if (!strcmp(path, "create"))
		temp = create_tempfile(git_path("sharedindex_XXXXXX"));
	else if (!strcmp(path, "mks"))
		temp = mks_tempfile(git_path("sharedindex_XXXXXX"));
	else if (!strcmp(path, "mks_sm"))
		temp = mks_tempfile_sm(git_path("sharedindex_XXXXXX"), 0, 0666);
	else
		die("unknown argument '%s'", path);

	if (close_tempfile_gently(temp))
		die("could not close '%s'", temp->filename.buf);

	if (stat(temp->filename.buf, &st))
		die("could not stat '%s'", temp->filename.buf);

	printf("mode for '%s': %o\n", temp->filename.buf, st.st_mode);
}

struct command {
	const char *name;
	int (*func)(const char **argv);
};

static struct command commands[] = {
	{ "tempfile", cmd_tempfile },
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
