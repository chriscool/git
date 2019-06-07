#include "test-tool.h"
#include "cache.h"
#include "oidmap.h"
#include "strbuf.h"

/* key is an oid and value is a refname */
struct test_entry {
	struct oidmap_entry entry;
	char refname[FLEX_ARRAY];
};

#define DELIM " \t\r\n"

/*
 * Read stdin line by line and print result of commands to stdout:
 *
 * hash key -> strhash(key) memhash(key) strihash(key) memihash(key)
 * put key value -> NULL / old value
 * get key -> NULL / value
 * remove key -> NULL / old value
 * iterate -> key1 value1\nkey2 value2\n...
 * size -> tablesize numentries
 *
 */
int cmd__oidmap(int argc, const char **argv)
{
	struct strbuf line = STRBUF_INIT;
	struct oidmap map = OIDMAP_INIT;

	setup_git_directory();

	/* init oidmap */
	oidmap_init(&map, 0);

	/* process commands from stdin */
	while (strbuf_getline(&line, stdin) != EOF) {
		char *cmd, *p1 = NULL, *p2 = NULL;
		struct test_entry *entry;
		struct object_id oid;

		/* break line into command and up to two parameters */
		cmd = strtok(line.buf, DELIM);
		/* ignore empty lines */
		if (!cmd || *cmd == '#')
			continue;

		p1 = strtok(NULL, DELIM);
		if (p1)
			p2 = strtok(NULL, DELIM);

		if (!strcmp("hash", cmd) && p1) {

			/* print hash of oid */
			if (!get_oid(p1, &oid))
				printf("%u\n", sha1hash(oid.hash));
			else
				printf("Could not convert '%s' to an object id!", p1);

		} else if (!strcmp("add", cmd) && p1 && p2) {

			if (get_oid(p1, &oid)) {
				printf("Could not convert '%s' to an object id!", p1);
				continue;
			}

			/* create entry with oidkey from p1, value = p2 */
			FLEX_ALLOC_STR(entry, refname, p2);
			oidcpy(&entry->entry.oid, &oid);

			/* add to oidmap */
			oidmap_put(&map, entry);

		} else if (!strcmp("put", cmd) && p1 && p2) {

			if (get_oid(p1, &oid)) {
				printf("Could not convert '%s' to an object id!", p1);
				continue;
			}

			/* create entry with oid_key = p1, refname_value = p2 */
			FLEX_ALLOC_STR(entry, refname, p2);
			oidcpy(&entry->entry.oid, &oid);

			/* add / replace entry */
			entry = oidmap_put(&map, entry);

			/* print and free replaced entry, if any */
			puts(entry ? entry->refname : "NULL");
			free(entry);

		} else if (!strcmp("get", cmd) && p1) {

			if (get_oid(p1, &oid)) {
				printf("Could not convert '%s' to an object id!", p1);
				continue;
			}

			/* lookup entry in oidmap */
			entry = oidmap_get(&map, &oid);

			/* print result */
			puts(entry ? entry->refname : "NULL");

		} else if (!strcmp("remove", cmd) && p1) {

			if (get_oid(p1, &oid)) {
				printf("Could not convert '%s' to an object id!", p1);
				continue;
			}

			/* remove entry from oidmap */
			entry = oidmap_remove(&map, &oid);

			/* print result and free entry*/
			puts(entry ? entry->refname : "NULL");
			free(entry);

		} else if (!strcmp("iterate", cmd)) {

			struct oidmap_iter iter;
			oidmap_iter_init(&map, &iter);
			while ((entry = oidmap_iter_next(&iter)))
				printf("%s %s\n", oid_to_hex(&entry->entry.oid), entry->refname);

		} else {

			printf("Unknown command %s\n", cmd);

		}
	}

	strbuf_release(&line);
	oidmap_free(&map, 1);
	return 0;
}
