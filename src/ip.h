// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_IP_H
#define REDD_IP_H

#include <msgpack.h>

typedef struct red_ip_s {
	char 				*ip_address;
} red_ip_t;

void		*dup_ip(void *ptr);
void		free_ip(void *ptr);
int			match_ip(void *ptr, void *key);

void		pack_ip(msgpack_packer *packer, red_ip_t *ip);
void		pack_ips(msgpack_packer *packer);
red_ip_t	*unpack_ip(msgpack_object object);
void		unpack_ips(msgpack_object object);
int			validate_ip(red_ip_t *ip);

void		save_ips();
void		load_ips();

int			add_red_ip(red_ip_t *ip);
int			remove_red_ip(red_ip_t *key);

#endif
