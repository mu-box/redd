// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_ROUTE_H
#define REDD_ROUTE_H

void handle_local_frame(char *frame, int len);
void init_routing();
void shutdown_routing();

#endif
