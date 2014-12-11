#ifndef __NOOLITE_CONFIG_SERVER_H
#define __NOOLITE_CONFIG_SERVER_H

#include "c_types.h"

// private
static void noolite_config_server_process_page(struct espconn *, char *, char *);
static unsigned char noolite_config_server_get_key_val(char *, unsigned char, char *, char *);
static void noolite_config_server_sent(void *);
static void noolite_config_server_recon(void *, sint8);
static void noolite_config_server_discon(void *);
static void noolite_config_server_recv(void *, char *, unsigned short);
static void noolite_config_server_listen(void *);

// public
void noolite_config_server_init();

#endif
