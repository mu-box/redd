// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_API_H
#define REDD_API_H

#include <uv.h>
#include <bframe.h>
#include <msgxchng.h>

typedef struct {
	uv_stream_t 		*stream;
	bframe_buffer_t		*buf;
} api_client_t;

typedef struct reply_data_s {
	char 	*key;
	int 	key_len;
	char 	*value;
	int 	value_len;
} reply_data_t;

void	init_api(void);
void	reply(api_client_t *client, msgxchng_response_t *res);
char	*pack_reply_data(reply_data_t *return_data, int elements, int *size);
void	reply_error(api_client_t *client, msgxchng_request_t *req, char *error_message);
void	reply_success(api_client_t *client, msgxchng_request_t *req);
void	shutdown_api();

#endif
