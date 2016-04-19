#ifndef __NOOLITE_PLATFORM_H
#define __NOOLITE_PLATFORM_H

#include "c_types.h"

#define NOOLITE_LOGGING

// Firmware version
#define ESPOOLITE_VERSION 1.1

// Max amount of connections
#define MAX_CONN 8

//Max send buffer len
#define MAX_SENDBUFF_LEN 2048

// Button to enter configuration mode of ESP8266
#define BTN_CONFIG_GPIO 0

#define recvTaskQueueLen 32

// MAGIC VALUE. When settings exist in flash this is the valid-flag.
#define SETUP_OK_KEY 0xBB2368AA

typedef enum {
	WIFI_CONNECTING,
	WIFI_CONNECTING_ERROR,
	WIFI_CONNECTED
} tConnState;

// WARNING: this structure's memory amount must be dividable by 4 in order to save to FLASH memory!!!
typedef struct {
	unsigned long SetupOk;
	char deviceId[16];
} tSetup;

// private
static void noolite_platform_check_ip(void *);

// public
void noolite_platform_init(void);

#endif
