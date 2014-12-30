#ifndef __NOOLITE_CONFIG_SERVER_H
#define __NOOLITE_CONFIG_SERVER_H

#include "c_types.h"
#include "utils.h"

// private
static void noolite_config_server_process_page(struct HttpdConnData *, char *, char *);
static unsigned char noolite_config_server_get_key_val(char *, unsigned char, char *, char *);
static void noolite_config_server_sent(void *);
static void noolite_config_server_recon(void *, sint8);
static void noolite_config_server_discon(void *);
static void noolite_config_server_recv(void *, char *, unsigned short);
static void noolite_config_server_listen(void *);
static HttpdConnData *config_httpdFindConnData(void *arg);
static void config_xmitSendBuff(HttpdConnData *conn);
int config_httpdSend(HttpdConnData *conn, const char *data, int len);
void config_httpdStartResponse(HttpdConnData *conn, int code);
void config_httpdHeader(HttpdConnData *conn, const char *field, const char *val);
void config_httpdEndHeaders(HttpdConnData *conn);
static void config_httpdRetireConn(HttpdConnData *conn);

// public
void noolite_config_server_init();

#endif
