#ifndef FETCH_OBJECT_H
#define FETCH_OBJECT_H

int fetch_objects(const char *remote_name, const struct object_id *oids,
		  int oid_nr);

#endif
