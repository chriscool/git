/*
 * git gc builtin command
 *
 * Cleanup unreachable files and optimize the repository.
 *
 * Copyright (c) 2007 James Bowes
 *
 * Based on git-gc.sh, which is
 *
 * Copyright (c) 2006 Shawn O. Pearce
 */

#include "builtin.h"
#include "tempfile.h"
#include "lockfile.h"
#include "parse-options.h"
#include "run-command.h"
#include "sigchain.h"
#include "argv-array.h"
#include "commit.h"

#define FAILED_RUN "failed to run %s"

static const char * const builtin_gc_usage[] = {
	N_("git gc [<options>]"),
	NULL
};

static int pack_refs = 1;
static int prune_reflogs = 1;
static int aggressive_depth = 250;
static int aggressive_window = 250;
static int gc_auto_threshold = 6700;
static int gc_auto_pack_limit = 50;
static int detach_auto = 1;
static const char *prune_expire = "2.weeks.ago";
static const char *prune_worktrees_expire = "3.months.ago";

static struct argv_array pack_refs_cmd = ARGV_ARRAY_INIT;
static struct argv_array reflog = ARGV_ARRAY_INIT;
static struct argv_array repack = ARGV_ARRAY_INIT;
static struct argv_array prune = ARGV_ARRAY_INIT;
static struct argv_array prune_worktrees = ARGV_ARRAY_INIT;
static struct argv_array rerere = ARGV_ARRAY_INIT;

static struct tempfile pidfile;
static struct lock_file log_lock;

static void git_config_date_string(const char *key, const char **output)
{
	if (git_config_get_string_const(key, output))
		return;
	if (strcmp(*output, "now")) {
		unsigned long now = approxidate("now");
		if (approxidate(*output) >= now)
			git_die_config(key, _("Invalid %s: '%s'"), key, *output);
	}
}

static void process_log_file(void)
{
	struct stat st;
	if (!fstat(get_lock_file_fd(&log_lock), &st) && st.st_size)
		commit_lock_file(&log_lock);
	else
		rollback_lock_file(&log_lock);
}

static void process_log_file_at_exit(void)
{
	fflush(stderr);
	process_log_file();
}

static void process_log_file_on_signal(int signo)
{
	process_log_file();
	sigchain_pop(signo);
	raise(signo);
}

static void gc_config(void)
{
	const char *value;

	if (!git_config_get_value("gc.packrefs", &value)) {
		if (value && !strcmp(value, "notbare"))
			pack_refs = -1;
		else
			pack_refs = git_config_bool("gc.packrefs", value);
	}

	git_config_get_int("gc.aggressivewindow", &aggressive_window);
	git_config_get_int("gc.aggressivedepth", &aggressive_depth);
	git_config_get_int("gc.auto", &gc_auto_threshold);
	git_config_get_int("gc.autopacklimit", &gc_auto_pack_limit);
	git_config_get_bool("gc.autodetach", &detach_auto);
	git_config_date_string("gc.pruneexpire", &prune_expire);
	git_config_date_string("gc.worktreepruneexpire", &prune_worktrees_expire);
	git_config(git_default_config, NULL);
}

static int too_many_loose_objects(void)
{
	/*
	 * Quickly check if a "gc" is needed, by estimating how
	 * many loose objects there are.  Because SHA-1 is evenly
	 * distributed, we can check only one and get a reasonable
	 * estimate.
	 */
	char path[PATH_MAX];
	const char *objdir = get_object_directory();
	DIR *dir;
	struct dirent *ent;
	int auto_threshold;
	int num_loose = 0;
	int needed = 0;

	if (gc_auto_threshold <= 0)
		return 0;

	if (sizeof(path) <= snprintf(path, sizeof(path), "%s/17", objdir)) {
		warning(_("insanely long object directory %.*s"), 50, objdir);
		return 0;
	}
	dir = opendir(path);
	if (!dir)
		return 0;

	auto_threshold = (gc_auto_threshold + 255) / 256;
	while ((ent = readdir(dir)) != NULL) {
		if (strspn(ent->d_name, "0123456789abcdef") != 38 ||
		    ent->d_name[38] != '\0')
			continue;
		if (++num_loose > auto_threshold) {
			needed = 1;
			break;
		}
	}
	closedir(dir);
	return needed;
}

static int too_many_packs(void)
{
	struct packed_git *p;
	int cnt;

	if (gc_auto_pack_limit <= 0)
		return 0;

	prepare_packed_git();
	for (cnt = 0, p = packed_git; p; p = p->next) {
		if (!p->pack_local)
			continue;
		if (p->pack_keep)
			continue;
		/*
		 * Perhaps check the size of the pack and count only
		 * very small ones here?
		 */
		cnt++;
	}
	return gc_auto_pack_limit <= cnt;
}

static void add_repack_all_option(void)
{
	if (prune_expire && !strcmp(prune_expire, "now"))
		argv_array_push(&repack, "-a");
	else {
		argv_array_push(&repack, "-A");
		if (prune_expire)
			argv_array_pushf(&repack, "--unpack-unreachable=%s", prune_expire);
	}
}

static int need_to_gc(void)
{
	/*
	 * Setting gc.auto to 0 or negative can disable the
	 * automatic gc.
	 */
	if (gc_auto_threshold <= 0)
		return 0;

	/*
	 * If there are too many loose objects, but not too many
	 * packs, we run "repack -d -l".  If there are too many packs,
	 * we run "repack -A -d -l".  Otherwise we tell the caller
	 * there is no need.
	 */
	if (too_many_packs())
		add_repack_all_option();
	else if (!too_many_loose_objects())
		return 0;

	if (run_hook_le(NULL, "pre-auto-gc", NULL))
		return 0;
	return 1;
}

/* return NULL on success, else hostname running the gc */
static const char *lock_repo_for_gc(int force, pid_t* ret_pid)
{
	static struct lock_file lock;
	char my_host[128];
	struct strbuf sb = STRBUF_INIT;
	struct stat st;
	uintmax_t pid;
	FILE *fp;
	int fd;
	char *pidfile_path;

	if (is_tempfile_active(&pidfile))
		/* already locked */
		return NULL;

	if (gethostname(my_host, sizeof(my_host)))
		xsnprintf(my_host, sizeof(my_host), "unknown");

	pidfile_path = git_pathdup("gc.pid");
	fd = hold_lock_file_for_update(&lock, pidfile_path,
				       LOCK_DIE_ON_ERROR);
	if (!force) {
		static char locking_host[128];
		int should_exit;
		fp = fopen(pidfile_path, "r");
		memset(locking_host, 0, sizeof(locking_host));
		should_exit =
			fp != NULL &&
			!fstat(fileno(fp), &st) &&
			/*
			 * 12 hour limit is very generous as gc should
			 * never take that long. On the other hand we
			 * don't really need a strict limit here,
			 * running gc --auto one day late is not a big
			 * problem. --force can be used in manual gc
			 * after the user verifies that no gc is
			 * running.
			 */
			time(NULL) - st.st_mtime <= 12 * 3600 &&
			fscanf(fp, "%"SCNuMAX" %127c", &pid, locking_host) == 2 &&
			/* be gentle to concurrent "gc" on remote hosts */
			(strcmp(locking_host, my_host) || !kill(pid, 0) || errno == EPERM);
		if (fp != NULL)
			fclose(fp);
		if (should_exit) {
			if (fd >= 0)
				rollback_lock_file(&lock);
			*ret_pid = pid;
			free(pidfile_path);
			return locking_host;
		}
	}

	strbuf_addf(&sb, "%"PRIuMAX" %s",
		    (uintmax_t) getpid(), my_host);
	write_in_full(fd, sb.buf, sb.len);
	strbuf_release(&sb);
	commit_lock_file(&lock);
	register_tempfile(&pidfile, pidfile_path);
	free(pidfile_path);
	return NULL;
}

static int report_last_gc_error(void)
{
	struct strbuf sb = STRBUF_INIT;
	int ret;

	ret = strbuf_read_file(&sb, git_path("gc.log"), 0);
	if (ret > 0)
		return error(_("The last gc run reported the following. "
			       "Please correct the root cause\n"
			       "and remove %s.\n"
			       "Automatic cleanup will not be performed "
			       "until the file is removed.\n\n"
			       "%s"),
			     git_path("gc.log"), sb.buf);
	strbuf_release(&sb);
	return 0;
}

static int gc_before_repack(void)
{
	if (pack_refs && run_command_v_opt(pack_refs_cmd.argv, RUN_GIT_CMD))
		return error(FAILED_RUN, pack_refs_cmd.argv[0]);

	if (prune_reflogs && run_command_v_opt(reflog.argv, RUN_GIT_CMD))
		return error(FAILED_RUN, reflog.argv[0]);

	pack_refs = 0;
	prune_reflogs = 0;
	return 0;
}

int cmd_gc(int argc, const char **argv, const char *prefix)
{
	int aggressive = 0;
	int auto_gc = 0;
	int quiet = 0;
	int force = 0;
	const char *name;
	pid_t pid;
	int daemonized = 0;

	struct option builtin_gc_options[] = {
		OPT__QUIET(&quiet, N_("suppress progress reporting")),
		{ OPTION_STRING, 0, "prune", &prune_expire, N_("date"),
			N_("prune unreferenced objects"),
			PARSE_OPT_OPTARG, NULL, (intptr_t)prune_expire },
		OPT_BOOL(0, "aggressive", &aggressive, N_("be more thorough (increased runtime)")),
		OPT_BOOL(0, "auto", &auto_gc, N_("enable auto-gc mode")),
		OPT_BOOL(0, "force", &force, N_("force running gc even if there may be another gc running")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	argv_array_pushl(&pack_refs_cmd, "pack-refs", "--all", "--prune", NULL);
	argv_array_pushl(&reflog, "reflog", "expire", "--all", NULL);
	argv_array_pushl(&repack, "repack", "-d", "-l", NULL);
	argv_array_pushl(&prune, "prune", "--expire", NULL);
	argv_array_pushl(&prune_worktrees, "worktree", "prune", "--expire", NULL);
	argv_array_pushl(&rerere, "rerere", "gc", NULL);

	gc_config();

	if (pack_refs < 0)
		pack_refs = !is_bare_repository();

	argc = parse_options(argc, argv, prefix, builtin_gc_options,
			     builtin_gc_usage, 0);
	if (argc > 0)
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	if (aggressive) {
		argv_array_push(&repack, "-f");
		if (aggressive_depth > 0)
			argv_array_pushf(&repack, "--depth=%d", aggressive_depth);
		if (aggressive_window > 0)
			argv_array_pushf(&repack, "--window=%d", aggressive_window);
	}
	if (quiet)
		argv_array_push(&repack, "-q");

	if (auto_gc) {
		/*
		 * Auto-gc should be least intrusive as possible.
		 */
		if (!need_to_gc())
			return 0;
		if (!quiet) {
			if (detach_auto)
				fprintf(stderr, _("Auto packing the repository in background for optimum performance.\n"));
			else
				fprintf(stderr, _("Auto packing the repository for optimum performance.\n"));
			fprintf(stderr, _("See \"git help gc\" for manual housekeeping.\n"));
		}
		if (detach_auto) {
			if (report_last_gc_error())
				return -1;

			if (gc_before_repack())
				return -1;
			/*
			 * failure to daemonize is ok, we'll continue
			 * in foreground
			 */
			daemonized = !daemonize(NULL);
		}
	} else
		add_repack_all_option();

	name = lock_repo_for_gc(force, &pid);
	if (name) {
		if (auto_gc)
			return 0; /* be quiet on --auto */
		die(_("gc is already running on machine '%s' pid %"PRIuMAX" (use --force if not)"),
		    name, (uintmax_t)pid);
	}

	if (daemonized) {
		hold_lock_file_for_update(&log_lock,
					  git_path("gc.log"),
					  LOCK_DIE_ON_ERROR);
		dup2(get_lock_file_fd(&log_lock), 2);
		sigchain_push_common(process_log_file_on_signal);
		atexit(process_log_file_at_exit);
	}

	if (gc_before_repack())
		return -1;

	if (!repository_format_precious_objects) {
		if (run_command_v_opt(repack.argv, RUN_GIT_CMD))
			return error(FAILED_RUN, repack.argv[0]);

		if (prune_expire) {
			argv_array_push(&prune, prune_expire);
			if (quiet)
				argv_array_push(&prune, "--no-progress");
			if (run_command_v_opt(prune.argv, RUN_GIT_CMD))
				return error(FAILED_RUN, prune.argv[0]);
		}
	}

	if (prune_worktrees_expire) {
		argv_array_push(&prune_worktrees, prune_worktrees_expire);
		if (run_command_v_opt(prune_worktrees.argv, RUN_GIT_CMD))
			return error(FAILED_RUN, prune_worktrees.argv[0]);
	}

	if (run_command_v_opt(rerere.argv, RUN_GIT_CMD))
		return error(FAILED_RUN, rerere.argv[0]);

	if (auto_gc && too_many_loose_objects())
		warning(_("There are too many unreachable loose objects; "
			"run 'git prune' to remove them."));

	return 0;
}
