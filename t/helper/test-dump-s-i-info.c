#include "cache.h"

static const char usage_str[] = "(write|delete|list|read) <args>...";
static const char write_usage_str[] = "write <shared-index> <path>";
static const char delete_usage_str[] = "delete <shared-index> <path>";
static const char list_usage_str[] = "list <shared-index>";
static const char read_usage_str[] = "read <shared-index> <path>";

#define SHAREDINDEX_INFO "sharedindex-info"

static void sha1_from_path(unsigned char *sha1, const char *path)
{
	git_SHA_CTX ctx;

	git_SHA1_Init(&ctx);
	git_SHA1_Update(&ctx, path, strlen(path));
	git_SHA1_Final(sha1, &ctx);
}

static void s_i_info_filename(struct strbuf *sb, const char *shared_index, const char *path)
{
	unsigned char path_sha1[GIT_SHA1_RAWSZ];

	sha1_from_path(path_sha1, path);
	strbuf_git_path(sb, SHAREDINDEX_INFO "/%s-%s",
			shared_index, sha1_to_hex(path_sha1));
}

static void write_s_i_info(const char *shared_index, const char *path)
{
	struct strbuf s_i_info = STRBUF_INIT;

	s_i_info_filename(&s_i_info, shared_index, path);

	switch (safe_create_leading_directories(s_i_info.buf))
	{
	case SCLD_OK:
		break; /* success */
	case SCLD_EXISTS:
		die("unable to create directory for '%s' "
		    "as a file with the same name as a directory already exists",
		    s_i_info.buf);
	case SCLD_VANISHED:
		die("unable to create directory for '%s' "
		    "as an underlying directory was just pruned; "
		    "maybe try again?",
		    s_i_info.buf);
	case SCLD_PERMS:
		die("unable to create directory for '%s' "
		    "because of permission problems",
		    s_i_info.buf);
	default:
		die("unable to create directory for '%s'", s_i_info.buf);
	}

	write_file(s_i_info.buf, "%s", path);

	strbuf_release(&s_i_info);
}

static void handle_write_command(int ac, const char **av)
{
	if (ac != 4)
		die("%s\nusage: %s %s",
		    "write command requires exactly 2 arguments",
		    av[0], write_usage_str);

	write_s_i_info(av[2], av[3]);
}

static void delete_s_i_info(const char *shared_index, const char *path)
{
	struct strbuf s_i_info = STRBUF_INIT;

	s_i_info_filename(&s_i_info, shared_index, path);

	if (unlink(s_i_info.buf) && (errno != ENOENT))
		die_errno("unable to unlink: %s", s_i_info.buf);

	strbuf_release(&s_i_info);
}

static void handle_delete_command(int ac, const char **av)
{
	if (ac != 4)
		die("%s\nusage: %s %s",
		    "delete command requires exactly 2 arguments",
		    av[0], delete_usage_str);

	delete_s_i_info(av[2], av[3]);
}

static void read_s_i_info_into_list(const char *filename, struct string_list *paths)
{
	struct strbuf index_path = STRBUF_INIT;
	const char *path = git_path(SHAREDINDEX_INFO "/%s", filename);

	if (strbuf_read_file(&index_path, path, 0) < 0)
		die_errno(_("could not read '%s'"), path);
	string_list_append(paths, strbuf_detach(&index_path, NULL));
}

static void list_s_i_info(const char *shared_index, struct string_list *paths)
{
	struct dirent *de;
	DIR *dir = opendir(git_path(SHAREDINDEX_INFO));

	if (!dir) {
		if (errno == ENOENT)
			return;
		die_errno("could not open directory '%s'",
			  git_path(SHAREDINDEX_INFO));
	}

	while ((de = readdir(dir)) != NULL) {
		if (starts_with(de->d_name, shared_index))
			read_s_i_info_into_list(de->d_name, paths);
	}
	closedir(dir);
}

static void handle_list_command(int ac, const char **av)
{
	struct string_list paths = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;

	if (ac != 3)
		die("%s\nusage: %s %s",
		    "list command requires exactly 1 argument",
		    av[0], list_usage_str);

	list_s_i_info(av[2], &paths);

	printf("index paths:\n\n");
	for_each_string_list_item(item, &paths) {
		printf("%s", item->string);
	}

	string_list_clear(&paths, 0);
}

static void read_s_i_info(struct strbuf *sb,
			  const char *shared_index,
			  const char *path)
{
	struct strbuf s_i_info = STRBUF_INIT;

	s_i_info_filename(&s_i_info, shared_index, path);

	if (strbuf_read_file(sb, s_i_info.buf, 0) < 0)
		die_errno(_("could not read '%s'"), s_i_info.buf);

	strbuf_release(&s_i_info);
}

static void handle_read_command(int ac, const char **av)
{
	struct strbuf sb = STRBUF_INIT;

	if (ac != 4)
		die("%s\nusage: %s %s",
		    "read command requires exactly 2 arguments",
		    av[0], read_usage_str);

	read_s_i_info(&sb, av[2], av[3]);

	printf("%s", sb.buf);

	strbuf_release(&sb);
}

static void show_args(int ac, const char **av)
{
	int i;

	for (i = 0; i < ac; i++) {
		printf("av[%d]: %s\n", i, av[i]);
	}
}

int cmd_main(int ac, const char **av)
{
	const char *command;

	if (ac < 2)
		die("%s\nusage: %s %s", "too few arguments", av[0], usage_str);

	command = av[1];

	if (!strcmp(command, "write"))
		handle_write_command(ac, av);
	else if (!strcmp(command, "delete"))
		handle_delete_command(ac, av);
	else if (!strcmp(command, "list"))
		handle_list_command(ac, av);
	else if (!strcmp(command, "read"))
		handle_read_command(ac, av);
	else
		show_args(ac, av);

	return 0;
}
