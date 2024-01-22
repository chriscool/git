#include "git-compat-util.h"
#include "missing.h"
#include "object-file.h"

int parse_missing_action_value(const char *value)
{
	if (!strcmp(value, "error"))
		return MA_ERROR;

	if (!strcmp(value, "allow-any"))
		return MA_ALLOW_ANY;

	if (!strcmp(value, "print"))
		return MA_PRINT;

	if (!strcmp(value, "allow-promisor"))
		return MA_ALLOW_PROMISOR;

	return -1;
}
