#include "cache.h"
/*
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 */

enum action_where { WHERE_AFTER, WHERE_BEFORE };
enum action_if_exists { EXISTS_ADD_IF_DIFFERENT, EXISTS_ADD_IF_DIFFERENT_NEIGHBOR,
			EXISTS_ADD, EXISTS_OVERWRITE, EXISTS_DO_NOTHING };
enum action_if_missing { MISSING_ADD, MISSING_DO_NOTHING };

struct conf_info {
	char *name;
	char *key;
	char *command;
	enum action_where where;
	enum action_if_exists if_exists;
	enum action_if_missing if_missing;
};

struct trailer_item {
	struct trailer_item *previous;
	struct trailer_item *next;
	const char *token;
	const char *value;
	struct conf_info conf;
};

static struct trailer_item *first_conf_item;

static int same_token(struct trailer_item *a, struct trailer_item *b, int alnum_len)
{
	return !strncasecmp(a->token, b->token, alnum_len);
}

static int same_value(struct trailer_item *a, struct trailer_item *b)
{
	return !strcasecmp(a->value, b->value);
}

static int same_trailer(struct trailer_item *a, struct trailer_item *b, int alnum_len)
{
	return same_token(a, b, alnum_len) && same_value(a, b);
}

/* Get the length of buf from its beginning until its last alphanumeric character */
static size_t alnum_len(const char *buf, size_t len)
{
	while (len > 0 && !isalnum(buf[len - 1]))
		len--;
	return len;
}

static void free_trailer_item(struct trailer_item *item)
{
	free(item->conf.name);
	free(item->conf.key);
	free(item->conf.command);
	free((char *)item->token);
	free((char *)item->value);
	free(item);
}

static void add_arg_to_input_list(struct trailer_item *in_tok,
				  struct trailer_item *arg_tok)
{
	if (arg_tok->conf.where == WHERE_AFTER) {
		arg_tok->next = in_tok->next;
		in_tok->next = arg_tok;
		arg_tok->previous = in_tok;
		if (arg_tok->next)
			arg_tok->next->previous = arg_tok;
	} else {
		arg_tok->previous = in_tok->previous;
		in_tok->previous = arg_tok;
		arg_tok->next = in_tok;
		if (arg_tok->previous)
			arg_tok->previous->next = arg_tok;
	}
}

static int check_if_different(struct trailer_item *in_tok,
			      struct trailer_item *arg_tok,
			      int alnum_len, int check_all)
{
	enum action_where where = arg_tok->conf.where;
	do {
		if (!in_tok)
			return 1;
		if (same_trailer(in_tok, arg_tok, alnum_len))
			return 0;
		/*
		 * if we want to add a trailer after another one,
		 * we have to check those before this one
		 */
		in_tok = (where == WHERE_AFTER) ? in_tok->previous : in_tok->next;
	} while (check_all);
	return 1;
}

static void apply_arg_if_exists(struct trailer_item *in_tok,
				struct trailer_item *arg_tok,
				int alnum_len)
{
	switch (arg_tok->conf.if_exists) {
	case EXISTS_DO_NOTHING:
		free_trailer_item(arg_tok);
		break;
	case EXISTS_OVERWRITE:
		free((char *)in_tok->value);
		in_tok->value = xstrdup(arg_tok->value);
		free_trailer_item(arg_tok);
		break;
	case EXISTS_ADD:
		add_arg_to_input_list(in_tok, arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT:
		if (check_if_different(in_tok, arg_tok, alnum_len, 1))
			add_arg_to_input_list(in_tok, arg_tok);
		else
			free_trailer_item(arg_tok);
		break;
	case EXISTS_ADD_IF_DIFFERENT_NEIGHBOR:
		if (check_if_different(in_tok, arg_tok, alnum_len, 0))
			add_arg_to_input_list(in_tok, arg_tok);
		else
			free_trailer_item(arg_tok);
		break;
	}
}

static void remove_from_list(struct trailer_item *item,
			     struct trailer_item **first)
{
	if (item->next)
		item->next->previous = item->previous;
	if (item->previous)
		item->previous->next = item->next;
	else
		*first = item->next;
}

static struct trailer_item *remove_first(struct trailer_item **first)
{
	struct trailer_item *item = *first;
	*first = item->next;
	if (item->next) {
		item->next->previous = NULL;
		item->next = NULL;
	}
	return item;
}

static void process_input_token(struct trailer_item *in_tok,
				struct trailer_item **arg_tok_first,
				enum action_where where)
{
	struct trailer_item *arg_tok;
	struct trailer_item *next_arg;

	int after = where == WHERE_AFTER;
	int tok_alnum_len = alnum_len(in_tok->token, strlen(in_tok->token));

	for (arg_tok = *arg_tok_first; arg_tok; arg_tok = next_arg) {
		next_arg = arg_tok->next;
		if (!same_token(in_tok, arg_tok, tok_alnum_len))
			continue;
		if (arg_tok->conf.where != where)
			continue;
		remove_from_list(arg_tok, arg_tok_first);
		apply_arg_if_exists(in_tok, arg_tok, tok_alnum_len);
		/*
		 * If arg has been added to input,
		 * then we need to process it too now.
		 */
		if ((after ? in_tok->next : in_tok->previous) == arg_tok)
			in_tok = arg_tok;
	}
}

static void update_last(struct trailer_item **last)
{
	if (*last)
		while ((*last)->next != NULL)
			*last = (*last)->next;
}

static void update_first(struct trailer_item **first)
{
	if (*first)
		while ((*first)->previous != NULL)
			*first = (*first)->previous;
}

static void apply_arg_if_missing(struct trailer_item **in_tok_first,
				 struct trailer_item **in_tok_last,
				 struct trailer_item *arg_tok)
{
	struct trailer_item **in_tok;
	enum action_where where;

	switch (arg_tok->conf.if_missing) {
	case MISSING_DO_NOTHING:
		free_trailer_item(arg_tok);
		break;
	case MISSING_ADD:
		where = arg_tok->conf.where;
		in_tok = (where == WHERE_AFTER) ? in_tok_last : in_tok_first;
		if (*in_tok) {
			add_arg_to_input_list(*in_tok, arg_tok);
			*in_tok = arg_tok;
		} else {
			*in_tok_first = arg_tok;
			*in_tok_last = arg_tok;
		}
		break;
	}
}

static void process_trailers_lists(struct trailer_item **in_tok_first,
				   struct trailer_item **in_tok_last,
				   struct trailer_item **arg_tok_first)
{
	struct trailer_item *in_tok;
	struct trailer_item *arg_tok;

	if (!*arg_tok_first)
		return;

	/* Process input from end to start */
	for (in_tok = *in_tok_last; in_tok; in_tok = in_tok->previous)
		process_input_token(in_tok, arg_tok_first, WHERE_AFTER);

	update_last(in_tok_last);

	if (!*arg_tok_first)
		return;

	/* Process input from start to end */
	for (in_tok = *in_tok_first; in_tok; in_tok = in_tok->next)
		process_input_token(in_tok, arg_tok_first, WHERE_BEFORE);

	update_first(in_tok_first);

	/* Process args left */
	while (*arg_tok_first) {
		arg_tok = remove_first(arg_tok_first);
		apply_arg_if_missing(in_tok_first, in_tok_last, arg_tok);
	}
}

static int set_where(struct conf_info *item, const char *value)
{
	if (!strcasecmp("after", value))
		item->where = WHERE_AFTER;
	else if (!strcasecmp("before", value))
		item->where = WHERE_BEFORE;
	else
		return 1;
	return 0;
}

static int set_if_exists(struct conf_info *item, const char *value)
{
	if (!strcasecmp("addIfDifferent", value))
		item->if_exists = EXISTS_ADD_IF_DIFFERENT;
	else if (!strcasecmp("addIfDifferentNeighbor", value))
		item->if_exists = EXISTS_ADD_IF_DIFFERENT_NEIGHBOR;
	else if (!strcasecmp("add", value))
		item->if_exists = EXISTS_ADD;
	else if (!strcasecmp("overwrite", value))
		item->if_exists = EXISTS_OVERWRITE;
	else if (!strcasecmp("doNothing", value))
		item->if_exists = EXISTS_DO_NOTHING;
	else
		return 1;
	return 0;
}

static int set_if_missing(struct conf_info *item, const char *value)
{
	if (!strcasecmp("doNothing", value))
		item->if_missing = MISSING_DO_NOTHING;
	else if (!strcasecmp("add", value))
		item->if_missing = MISSING_ADD;
	else
		return 1;
	return 0;
}

enum trailer_info_type { TRAILER_KEY, TRAILER_COMMAND, TRAILER_WHERE,
			 TRAILER_IF_EXISTS, TRAILER_IF_MISSING };

static int set_name_and_type(const char *conf_key, const char *suffix,
			     enum trailer_info_type type,
			     char **pname, enum trailer_info_type *ptype)
{
	int ret = ends_with(conf_key, suffix);
	if (ret) {
		*pname = xstrndup(conf_key, strlen(conf_key) - strlen(suffix));
		*ptype = type;
	}
	return ret;
}

static struct trailer_item *get_conf_item(const char *name)
{
	struct trailer_item *item;
	struct trailer_item *previous;

	/* Look up item with same name */
	for (previous = NULL, item = first_conf_item;
	     item;
	     previous = item, item = item->next) {
		if (!strcasecmp(item->conf.name, name))
			return item;
	}

	/* Item does not already exists, create it */
	item = xcalloc(sizeof(struct trailer_item), 1);
	item->conf.name = xstrdup(name);

	if (!previous)
		first_conf_item = item;
	else {
		previous->next = item;
		item->previous = previous;
	}

	return item;
}

static int git_trailer_config(const char *conf_key, const char *value, void *cb)
{
	if (starts_with(conf_key, "trailer.")) {
		const char *orig_conf_key = conf_key;
		struct trailer_item *item;
		struct conf_info *conf;
		char *name;
		enum trailer_info_type type;

		conf_key += 8;
		if (!set_name_and_type(conf_key, ".key", TRAILER_KEY, &name, &type) &&
		    !set_name_and_type(conf_key, ".command", TRAILER_COMMAND, &name, &type) &&
		    !set_name_and_type(conf_key, ".where", TRAILER_WHERE, &name, &type) &&
		    !set_name_and_type(conf_key, ".ifexists", TRAILER_IF_EXISTS, &name, &type) &&
		    !set_name_and_type(conf_key, ".ifmissing", TRAILER_IF_MISSING, &name, &type))
			return 0;

		item = get_conf_item(name);
		conf = &item->conf;
		free(name);

		switch (type) {
		case TRAILER_KEY:
			if (conf->key)
				warning(_("more than one %s"), orig_conf_key);
			conf->key = xstrdup(value);
			break;
		case TRAILER_COMMAND:
			if (conf->command)
				warning(_("more than one %s"), orig_conf_key);
			conf->command = xstrdup(value);
			break;
		case TRAILER_WHERE:
			if (set_where(conf, value))
				warning(_("unknown value '%s' for key '%s'"), value, orig_conf_key);
			break;
		case TRAILER_IF_EXISTS:
			if (set_if_exists(conf, value))
				warning(_("unknown value '%s' for key '%s'"), value, orig_conf_key);
			break;
		case TRAILER_IF_MISSING:
			if (set_if_missing(conf, value))
				warning(_("unknown value '%s' for key '%s'"), value, orig_conf_key);
			break;
		default:
			die("internal bug in trailer.c");
		}
	}
	return 0;
}
