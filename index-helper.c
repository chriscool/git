#include "cache.h"
#include "parse-options.h"
#include "sigchain.h"
#include "exec_cmd.h"
#include "split-index.h"
#include "shm.h"
#include "lockfile.h"

struct shm {
	unsigned char sha1[20];
	void *shm;
	size_t size;
};

static struct shm shm_index;
static struct shm shm_base_index;

static void release_index_shm(struct shm *is)
{
	if (!is->shm)
		return;
	munmap(is->shm, is->size);
	git_shm_unlink("git-index-%s", sha1_to_hex(is->sha1));
	is->shm = NULL;
}

static void cleanup_shm(void)
{
	release_index_shm(&shm_index);
	release_index_shm(&shm_base_index);
}

static void cleanup(void)
{
	unlink(git_path("index-helper.pid"));
	cleanup_shm();
}

static void cleanup_on_signal(int sig)
{
	cleanup();
	sigchain_pop(sig);
	raise(sig);
}

static void share_index(struct index_state *istate, struct shm *is)
{
	void *new_mmap;
	if (istate->mmap_size <= 20 ||
	    hashcmp(istate->sha1,
		    (unsigned char *)istate->mmap + istate->mmap_size - 20) ||
	    !hashcmp(istate->sha1, is->sha1) ||
	    git_shm_map(O_CREAT | O_EXCL | O_RDWR, 0700, istate->mmap_size,
			&new_mmap, PROT_READ | PROT_WRITE, MAP_SHARED,
			"git-index-%s", sha1_to_hex(istate->sha1)) < 0)
		return;

	release_index_shm(is);
	is->size = istate->mmap_size;
	is->shm = new_mmap;
	hashcpy(is->sha1, istate->sha1);
	memcpy(new_mmap, istate->mmap, istate->mmap_size - 20);

	/*
	 * The trailing hash must be written last after everything is
	 * written. It's the indication that the shared memory is now
	 * ready.
	 */
	hashcpy((unsigned char *)new_mmap + istate->mmap_size - 20, is->sha1);
}

static void share_the_index(void)
{
	if (the_index.split_index && the_index.split_index->base)
		share_index(the_index.split_index->base, &shm_base_index);
	share_index(&the_index, &shm_index);
	discard_index(&the_index);
}

static void refresh(int sig)
{
	the_index.keep_mmap = 1;
	the_index.to_shm    = 1;
	if (read_cache() < 0)
		die(_("could not read index"));
	share_the_index();
}

#ifdef HAVE_SHM

static void do_nothing(int sig)
{
	/*
	 * what we need is the signal received and interrupts
	 * sleep(). We don't need to do anything else when receving
	 * the signal
	 */
}

static void loop(const char *pid_file, int idle_in_seconds)
{
	sigchain_pop(SIGHUP);	/* pushed by sigchain_push_common */
	sigchain_push(SIGHUP, refresh);
	sigchain_push(SIGUSR1, do_nothing);
	refresh(0);
	while (sleep(idle_in_seconds))
		; /* do nothing, all is handled by signal handlers already */
}

#else

static void loop(const char *pid_file, int idle_in_seconds)
{
	die(_("index-helper is not supported on this platform"));
}

#endif

static const char * const usage_text[] = {
	N_("git index-helper [options]"),
	NULL
};

int main(int argc, char **argv)
{
	static struct lock_file lock;
	struct strbuf sb = STRBUF_INIT;
	const char *prefix;
	int fd, idle_in_minutes = 10;
	struct option options[] = {
		OPT_INTEGER(0, "exit-after", &idle_in_minutes,
			    N_("exit if not used after some minutes")),
		OPT_END()
	};

	git_extract_argv0_path(argv[0]);
	git_setup_gettext();

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(usage_text, options);

	prefix = setup_git_directory();
	if (parse_options(argc, (const char **)argv, prefix,
			  options, usage_text, 0))
		die(_("too many arguments"));

	fd = hold_lock_file_for_update(&lock,
				       git_path("index-helper.pid"),
				       LOCK_DIE_ON_ERROR);
	strbuf_addf(&sb, "%" PRIuMAX, (uintmax_t) getpid());
	write_in_full(fd, sb.buf, sb.len);
	commit_lock_file(&lock);

	atexit(cleanup);
	sigchain_push_common(cleanup_on_signal);

	if (!idle_in_minutes)
		idle_in_minutes = 0xffffffff / 60;
	loop(sb.buf, idle_in_minutes * 60);
	strbuf_release(&sb);
	return 0;
}
