#ifndef REF_UPDATE_ARRAY_H
#define REF_UPDATE_ARRAY_H

struct ref_update;

struct ref_update_array {
	struct ref_update **updates;
	size_t nr;
	size_t alloc;
};

#define REF_UPDATE_ARRAY_INIT { NULL, 0, 0 }

void ref_update_array_append(struct ref_update_array *array, struct ref_update *update);
void ref_update_array_clear(struct ref_update_array *array);

#endif /* REF_UPDATE_ARRAY_H */
