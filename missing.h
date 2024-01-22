#ifndef MISSING_H
#define MISSING_H

enum missing_action {
	MA_ERROR = 0,      /* fail if any missing objects are encountered */
	MA_ALLOW_ANY,      /* silently allow ALL missing objects */
	MA_PRINT,          /* print ALL missing objects in special section */
	MA_ALLOW_PROMISOR, /* silently allow all missing PROMISOR objects */
};

/*
  Return an `enum missing_action` in case parsing is successful or -1
  if parsing failed.
*/
int parse_missing_action_value(const char *value);

#endif /* MISSING_H */
