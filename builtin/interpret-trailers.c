/*
 * Builtin "git interpret-trailers"
 *
 * Copyright (c) 2013 Christian Couder <chriscool@tuxfamily.org>
 *
 */

#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "strbuf.h"

static const char * const git_interpret_trailers_usage[] = {
	N_("git interpret-trailers [--trim-empty] [--infile=file] [<token[=value]>...]"),
	NULL
};

static void parse_arg(struct strbuf *tok, struct strbuf *val, const char *arg)
{
	char *end = strchr(arg, '=');
	if (!end)
		end = strchr(arg, ':');
	if (end) {
		strbuf_add(tok, arg, end - arg);
		strbuf_trim(tok);
		strbuf_addstr(val, end + 1);
		strbuf_trim(val);
	} else {
		strbuf_addstr(tok, arg);
		strbuf_trim(tok);
	}
}

static struct string_list trailer_list;

enum trailer_conf { ADD, UNIQ };

struct trailer_info {
	char *value;
	char *command;
	enum trailer_conf conf;
};

static int git_trailer_config(const char *key, const char *value, void *cb)
{
	if (!prefixcmp(key, "trailer.")) {
		const char *orig_key = key;
		char *name;
		struct string_list_item *item;
		struct trailer_info *info;
		enum { VALUE, CONF, COMMAND } type;

		key += 8;
		if (!suffixcmp(key, ".value")) {
			name = xstrndup(key, strlen(key) - 6);
			type = VALUE;
		} else if (!suffixcmp(key, ".conf")) {
			name = xstrndup(key, strlen(key) - 5);
			type = CONF;
		} else if (!suffixcmp(key, ".command")) {
			name = xstrndup(key, strlen(key) - 8);
			type = COMMAND;
		} else
			return 0;

		item = string_list_insert(&trailer_list, name);

		if (!item->util)
			item->util = xcalloc(sizeof(struct trailer_info), 1);
		info = item->util;
		if (type == VALUE) {
			if (info->value)
				warning(_("more than one %s"), orig_key);
			info->value = xstrdup(value);
		} else if (type == CONF) {
			if (!strcasecmp("add", value)) {
				info->conf = ADD;
			} else if (!strcasecmp("uniq", value)) {
				info->conf = UNIQ;
			} else
				warning(_("unknow value '%s' for key '%s'"), value, orig_key);
		} else {
			if (info->command)
				warning(_("more than one %s"), orig_key);
			info->command = xstrdup(value);
		}
	}
	return 0;
}

static void apply_config(struct strbuf *tok, struct strbuf *val, struct trailer_info *info)
{
	if (info->value) {
		strbuf_reset(tok);
		strbuf_addstr(tok, info->value);
	}
	if (info->command) {
	}
}

static struct strbuf **read_input_file(const char *infile)
{
	struct strbuf sb = STRBUF_INIT;

	if (strbuf_read_file(&sb, infile, 0) < 0)
		die_errno(_("could not read input file '%s'"), infile);

	return strbuf_split(&sb, '\n');
}

/*
 * Return the the (0 based) index of the first trailer line
 * or the line count if there are no trailers.
 */
static int find_trailer_start(struct strbuf **lines)
{
	int count, start, empty = 1;

	/* Get the line count */
	for (count = 0; lines[count]; count++);

	/*
	 * Get the start of the trailers by looking starting from the end
	 * for a line with only spaces before lines with one ':'.
	 */
	for (start = count - 1; start >= 0; start--) {
		if (strbuf_isspace(lines[start])) {
			if (empty)
				continue;
			return start + 1;
		}
		if (strchr(lines[start]->buf, ':')) {
			if (empty)
				empty = 0;
			continue;
		}
		return count;
	}

	return empty ? count : start + 1;
}

static size_t alnum_len(const char *buf, size_t len) {
	while (--len >= 0 && !isalnum(buf[len]));
	return len + 1;
}

static void print_tok_val(const char *tok_buf, size_t tok_len,
			  const char *val_buf, size_t val_len)
{
	char c = tok_buf[tok_len - 1];
	if (isalnum(c))
		printf("%s: %s\n", tok_buf, val_buf);
	else if (isspace(c) || c == '#')
		printf("%s%s\n", tok_buf, val_buf);
	else
		printf("%s %s\n", tok_buf, val_buf);
}

static void process_input_file(const char *infile,
			       struct string_list *tok_list,
			       struct string_list *val_list)
{
	struct strbuf **lines = read_input_file(infile);
	int start = find_trailer_start(lines);
	int i;

	/* Output non trailer lines as is */
	for (i = 0; lines[i] && i < start; i++) {
		printf("%s", lines[i]->buf);
	}

	/* Process trailer lines */
	for (i = start; lines[i]; i++) {
		struct strbuf tok = STRBUF_INIT;
		struct strbuf val = STRBUF_INIT;
		parse_arg(&tok, &val, lines[i]->buf);
		string_list_append(tok_list, strbuf_detach(&tok, NULL));
		string_list_append(val_list, strbuf_detach(&val, NULL));
	}
}

int cmd_interpret_trailers(int argc, const char **argv, const char *prefix)
{
	const char *infile = NULL;
	int trim_empty = 0;
	int i;
	struct string_list tok_list = STRING_LIST_INIT_NODUP;
	struct string_list val_list = STRING_LIST_INIT_NODUP;

	struct option options[] = {
		OPT_BOOL(0, "trim-empty", &trim_empty, N_("trim empty trailers")),
		OPT_FILENAME(0, "infile", &infile, N_("use message from file")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	git_config(git_trailer_config, NULL);

	/* This prints the non trailer part of infile */
	if (infile)
		process_input_file(infile, &tok_list, &val_list);

	for (i = 0; i < argc; i++) {
		struct strbuf tok = STRBUF_INIT;
		struct strbuf val = STRBUF_INIT;
		int j, len;
		int seen = 0;

		parse_arg(&tok, &val, argv[i]);
		len = alnum_len(tok.buf, tok.len);

		for (j = 0; j < trailer_list.nr; j++) {
			struct string_list_item *item = trailer_list.items + j;
			struct trailer_info *info = item->util;
			if (!strncasecmp(tok.buf, item->string, len) ||
			    !strncasecmp(tok.buf, info->value, len)) {
				apply_config(&tok, &val, info);
				break;
			}
		}

		for (j = 0; j < tok_list.nr; j++) {
			struct string_list_item *tok_item = tok_list.items + j;
			struct string_list_item *val_item = val_list.items + j;
			if (!strncasecmp(tok.buf, tok_item->string, len)) {
				tok_item->string = xstrdup(tok.buf);
				val_item->string = xstrdup(val.buf);
				seen = 1;
				break;
			}
		}

		/* This prints the trailers passed as arguments that are not in infile */
		if (!seen && (!trim_empty || val.len > 0))
			print_tok_val(tok.buf, tok.len, val.buf, val.len);

		strbuf_release(&tok);
		strbuf_release(&val);
	}

	/* This prints the trailer part of infile */
	for (i = 0; i < tok_list.nr; i++) {
		struct string_list_item *tok_item = tok_list.items + i;
		struct string_list_item *val_item = val_list.items + i;
		if (!trim_empty || strlen(val_item->string) > 0)
			print_tok_val(tok_item->string, strlen(tok_item->string),
				      val_item->string, strlen(val_item->string));
	}

	return 0;
}
