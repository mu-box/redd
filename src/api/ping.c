// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#include <stdlib.h>
#include <msgxchng.h>

#include "api.h"
#include "api/ping.h"
#include "helper.h"
#include "log.h"

void
handle_ping(api_client_t *client, msgxchng_request_t *req)
{
	msgxchng_response_t *res;
	res = new_msgxchng_response(req->id, req->id_len, req->data, req->data_len, "complete", 8);

	red_log(REDD_DEBUG, "request %s: ping", req->id);

	reply(client, res);

	/* cleanup */
	clean_msgxchng_request(req);
	free(req);
	req = NULL;
}
