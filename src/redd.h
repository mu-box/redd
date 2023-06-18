// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#ifndef REDD_H
#define REDD_H

#include <uv.h>

#include "async_io.h"
#include "util/adlist.h"
#include "route.h"

// todo: move this to autobuild
#define REDD_VERSION					"0.0.2"

/* Error codes */
#define REDD_OK						0
#define REDD_ERR						-1

/* Limits */
#define REDD_CONFIGLINE_MAX			1024
#define REDD_MAX_LOGMSG_LEN			1024	/* Default maximum length of syslog messages */
#define REDD_BINDADDR_MAX				16

/* Sensible defaults */
#define REDD_DEFAULT_PID_FILE			"/var/run/red.pid"
#define REDD_DEFAULT_DAEMONIZE			0
#define REDD_DEFAULT_LOGFILE			""
#define REDD_DEFAULT_SYSLOG_IDENT		"red"
#define REDD_DEFAULT_SERVERPORT		4470	/* TCP port */
#define REDD_DEFAULT_MAXIDLETIME		0	/* default client timeout: infinite */
#define REDD_DEFAULT_MAX_CLIENTS		10000
#define REDD_DEFAULT_TCP_KEEPALIVE		0
#define REDD_DEFAULT_SYSLOG_ENABLED	0
#define REDD_DEFAULT_SAVE_PATH			"/var/db/red/"
#define REDD_DEFAULT_ROUTING_ENABLED	1
#define REDD_DEFAULT_UDP_PORT			8472
#define REDD_DEFAULT_UDP_RECV_BUF		114688
#define REDD_DEFAULT_UDP_SEND_BUF		114688
#define REDD_DEFAULT_VXLAN_MAX_RETRIES	10

typedef struct red_server_s {
	/* General */
	uv_loop_t			*loop;							/* Event loop */
	uv_signal_t			signal_handle;

	/* Udp */
	char				*udp_listen_address;			/* Address for the udp server to listen to */
	int					udp_recv_buf;					/* udp receive buffer */
	int					udp_send_buf;					/* udp send buffer */
	async_io_t			udp_async_io;

	/* Tun */
	char				*tun_name;
	int					tun_fd;
	async_io_t			tun_async_io;

	/* Configuration */
	char				*configfile;					/* Absolute config file path, or NULL */
	char				*pidfile;						/* PID file path */
	int					daemonize;						/* True if running as a daemon */

	/* Logging */
	int					verbosity;						/* Loglevel in red.conf */
	char				*logfile;						/* Path of log file */
	int					syslog_enabled;					/* Is syslog enabled? */
	char				*syslog_ident;					/* Syslog ident */
	int					syslog_facility;				/* Syslog facility */

	/* Networking */
	int					port;							/* TCP listening port */
	char				*bindaddr[REDD_BINDADDR_MAX];	/* Addresses we should bind to */
	int					bindaddr_count;					/* Number of addresses in server.bindaddr[] */
	uv_tcp_t			*ipfd[REDD_BINDADDR_MAX];		/* TCP socket file descriptors */
	int					ipfd_count;						/* Used slots in ipfd[] */
	int					maxidletime;					/* Client timeout in seconds */
	int					routing_enabled;				/* enable actual routing */

	/* Database */
	list				*nodes;							/* List of host nodes */
	list				*ips;							/* List of attached ips */

	/* VxLan */
	char				*vxlan_name;
	char				*vxlan_vni;
	char				*vxlan_group;
	char				*vxlan_port;
	char				*vxlan_interface;
	int					vxlan_max_retries;

	/* Save */
	char				*save_path;					/* Where to save things */
} red_server_t;

extern red_server_t server;

#endif
