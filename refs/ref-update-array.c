#include "cache.h"
#include "ref-update-array.h"

void ref_update_array_append(struct ref_update_array *array, struct ref_update *update)
{
	ALLOC_GROW(array->updates, array->nr + 1, array->alloc);
	array->updates[array->nr++] = update;
}

void ref_update_array_clear(struct ref_update_array *array)
{
	int i;
	for (i = 0; i < array->nr; i++)
		free(array->updates[i]);
}

