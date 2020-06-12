#include "commit.h"
#include "ref-filter.h"
#include "pretty-lib.h"

/**
 * Set value of `format->format` according to user_format 
 * 
 * TODO - Add support for more formatting options
*/
static size_t get_format_option(const char *placeholder, struct ref_format *format)
{
	switch (placeholder[0]) {	
	case 'H':
		format->format = "%(objectname)";
		return 1;
	case 'h':
		format->format = "%(objectname:short)";
		return 1;
	case 'T':
		format->format = "%(tree)";
		return 1;
	default:
		die(_("invalid formatting option"));
	}
	return 0;
}

void ref_pretty_print_commit(struct pretty_print_context *pp,
			 const struct commit *commit,
			 struct strbuf *sb)
{
	struct ref_format format = REF_FORMAT_INIT;
	const char *name = "refs";
	const char *usr_fmt = user_format; 

	if (pp->fmt == CMIT_FMT_USERFORMAT) {
		/* for getting each formatting option */
		for (;;) {
			const char *percent;
			size_t consumed;

			percent = strchrnul(usr_fmt, '%');
			strbuf_add(sb, usr_fmt, percent - usr_fmt);
			if (!*percent)
				break;
			usr_fmt = percent + 1;

			if (*usr_fmt == '%') {
				strbuf_addch(sb, '%');
				usr_fmt++;
				continue;
			}

			consumed = get_format_option(usr_fmt, &format);
			verify_ref_format(&format);
			pretty_print_ref(name, &commit->object.oid, &format);
			if (consumed)
				usr_fmt += consumed;
			else
				strbuf_addch(sb, '%');
		}
		return;
	} else if (pp->fmt == CMIT_FMT_DEFAULT || pp->fmt == CMIT_FMT_MEDIUM) {
		format.format = "Author: %(authorname) %(authoremail)\nDate:\t%(authordate)\n\n%(subject)\n\n%(body)";
	} else if (pp->fmt == CMIT_FMT_ONELINE) {
		format.format = "%(subject)";
	} else if (pp->fmt == CMIT_FMT_SHORT) {
		format.format = "Author: %(authorname) %(authoremail)\n\n\t%(subject)";
	} else if (pp->fmt == CMIT_FMT_FULL) {
		format.format = "Author: %(authorname) %(authoremail)\nCommit: %(committername) %(committeremail)\n\n%(subject)\n\n%(body)";
	} else if (pp->fmt == CMIT_FMT_FULLER) {
		format.format = "Author:\t\t%(authorname) %(authoremail)\nAuthorDate:\t%(authordate)\nCommit:\t\t%(committername) %(committeremail)\nCommitDate:\t%(committerdate)\n\n%(subject)\n\n%(body)";
	}

	verify_ref_format(&format);
	pretty_print_ref(name, &commit->object.oid, &format);
}
