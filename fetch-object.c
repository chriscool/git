#include "cache.h"
#include "packfile.h"
#include "pkt-line.h"
#include "strbuf.h"
#include "transport.h"
#include "fetch-object.h"

static int fetch_refs(const char *remote_name, struct ref *ref)
{
	struct remote *remote;
	struct transport *transport;
	int original_fetch_if_missing = fetch_if_missing;
	int res;

	fetch_if_missing = 0;
	remote = remote_get(remote_name);
	if (!remote->url[0])
		die(_("Remote with no URL"));
	transport = transport_get(remote, remote->url[0]);

	transport_set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
	transport_set_option(transport, TRANS_OPT_NO_DEPENDENTS, "1");
	res = transport_fetch_refs(transport, ref, NULL);
	fetch_if_missing = original_fetch_if_missing;

	return res;
}

int fetch_object(const char *remote_name, const unsigned char *sha1)
{
	struct ref *ref = alloc_ref(sha1_to_hex(sha1));
	hashcpy(ref->old_oid.hash, sha1);
	return fetch_refs(remote_name, ref);
}

int fetch_objects(const char *remote_name, const struct oid_array *to_fetch)
{
	struct ref *ref = NULL;
	int i;

	for (i = 0; i < to_fetch->nr; i++) {
		struct ref *new_ref = alloc_ref(oid_to_hex(&to_fetch->oid[i]));
		oidcpy(&new_ref->old_oid, &to_fetch->oid[i]);
		new_ref->next = ref;
		ref = new_ref;
	}
	return fetch_refs(remote_name, ref);
}
