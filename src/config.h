// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_CONFIG_H
#define REDD_CONFIG_H

void	init_server_config(void);
void	load_server_config(char *filename, char *options);
void	clean_server_config(void);

#endif
