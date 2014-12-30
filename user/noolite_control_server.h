#ifndef __NOOLITE_CONTROL_SERVER_H
#define __NOOLITE_CONTROL_SERVER_H

#include "c_types.h"
#include "utils.h"

#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))
#define sleepms(x) os_delay_us(x*1000);

// private
static int noolite_sendCommand(unsigned char channel, unsigned char command, unsigned char data, unsigned char format);
static void noolite_control_server_deviceid_page(struct HttpdConnData *, char *, char *);
static void noolite_control_server_process_page(struct HttpdConnData *, char *, char *);
static unsigned char noolite_control_server_get_key_val(char *, unsigned char, char *, char *);
static void noolite_control_server_sent(void *);
static void noolite_control_server_recon(void *, sint8);
static void noolite_control_server_discon(void *);
static void noolite_control_server_recv(void *, char *, unsigned short);
static void noolite_control_server_listen(void *);
static HttpdConnData *control_httpdFindConnData(void *arg);
static void control_xmitSendBuff(HttpdConnData *conn);
int control_httpdSend(HttpdConnData *conn, const char *data, int len);
void control_httpdStartResponse(HttpdConnData *conn, int code);
void control_httpdHeader(HttpdConnData *conn, const char *field, const char *val);
void control_httpdEndHeaders(HttpdConnData *conn);
static void control_httpdRetireConn(HttpdConnData *conn);

// public
void noolite_control_server_init();

#endif
