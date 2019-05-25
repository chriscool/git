#include "cache.h"
#include "builtin.h"

int cmd_psuh(int argc, const char **argv, const char *prefix)
{
	int i;

	printf(Q_("Your args (there is %d):\n",
		  "Your args (there are %d):\n",
		  argc),
	       argc);
	for (i = 0; i < argc; i++)
		printf("%d: %s\n", i, argv[i]);

	printf(_("Your current working directory:\n<top-level>%s%s\n"),
	       prefix ? "/" : "", prefix ? prefix : "");

	return 0;
}
