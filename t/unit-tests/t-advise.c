#include "test-lib.h"
#include "advice.h"
#include "config.h"
#include "setup.h"
#include "strbuf.h"

static const char expect_advice_msg[] = "hint: This is a piece of advice\n"
					"hint: Disable this message with \"git config advice.nestedTag false\"\n";
static const char advice_msg[] = "This is a piece of advice";
static const char out_file[] = "./output.txt";


static void check_advise_if_enabled(const char *argv, const char *conf_val, const char *expect) {
	FILE *file;
	struct strbuf actual = STRBUF_INIT;

	if (conf_val)
		git_default_advice_config("advice.nestedTag", conf_val);

	file = freopen(out_file, "w", stderr);
	if (!check(!!file)) {
		test_msg("Error opening file %s", out_file);
		return;
	}

	advise_if_enabled(ADVICE_NESTED_TAG, "%s", argv);
	fclose(file);

	if (!check(strbuf_read_file(&actual, out_file, 0) >= 0)) {
		test_msg("Error reading file %s", out_file);
		strbuf_release(&actual);
		return;
	}

	check_str(actual.buf, expect);
	strbuf_release(&actual);

	if (!check(remove(out_file) == 0))
		test_msg("Error deleting %s", out_file);
}

int cmd_main(int argc, const char **argv) {
	setenv("TERM", "dumb", 1);

	TEST(check_advise_if_enabled(advice_msg, NULL, expect_advice_msg),
		"advice should be printed when config variable is unset");
	TEST(check_advise_if_enabled(advice_msg, "true", expect_advice_msg),
		"advice should be printed when config variable is set to true");
	TEST(check_advise_if_enabled(advice_msg, "false", ""),
		"advice should not be printed when config variable is set to false");

	return test_done();
}
