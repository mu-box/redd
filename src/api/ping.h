// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_API_PING_H
#define REDD_API_PING_H

#include <msgxchng.h>

#include "api.h"

void handle_ping(api_client_t *client, msgxchng_request_t *req);

#endif
