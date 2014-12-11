#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

#include "noolite_config_server.h"
#include "flash_param.h"
#include "noolite_platform.h"

ETSTimer returnToNormalModeTimer;
static struct espconn esp_conn;
static esp_tcp esptcp;
static unsigned char killConn;
static unsigned char returnToNormalMode;
// http headers
static const char *http404Header = "HTTP/1.0 404 Not Found\r\nServer: nooLite-Config-Server\r\nContent-Type: text/plain\r\n\r\nNot Found (or method not implemented).\r\n";
static const char *http200Header = "HTTP/1.0 200 OK\r\nServer: nooLite-Config-Server/0.1\r\nContent-Type: text/html\r\n";
// html page header and footer
static const char *pageStart = "<html><head><title>nooLite base config</title><style>body{font-family: Arial}</style></head><body><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"save\" value=\"1\">\r\n";
static const char *pageEnd = "</form><hr>(c) 2014 by <a href=\"mailto:sleuthhound@gmail.com\" target=\"_blank\">Mikhail Grigorev</a>, <a href=\"http://programs74.ru\" target=\"_blank\">programs74.ru</a>\r\n</body></html>\r\n";
// html pages
static const char *pageIndex = "<h2>Welcome to nooLite base config</h2><ul><li><a href=\"?page=wifi\">WiFi settings</a></li><li><a href=\"?page=noolite\">nooLite settings</a></li><li><a href=\"?page=return\">Return to normal mode</a></li></ul>\r\n";
static const char *pageSetWifi = "<h2><a href=\"/\">Home</a> / WiFi settings</h2><input type=\"hidden\" name=\"page\" value=\"wifi\"><table border=\"0\"><tr><td><b>AP SSID:</b></td><td><input type=\"text\" name=\"ssid\" value=\"{ssid}\" size=\"40\"></td></tr><tr><td><b>AP Password:</b></td><td><input type=\"password\" name=\"pass\" value=\"***\" size=\"40\"></td></tr><tr><td><b>Status:</b></td><td>{status} <a href=\"?page=wifi\">[refresh]</a></td></tr><tr><td></td><td><input type=\"submit\" value=\"Save\"></td></tr></table>\r\n";
static const char *pageSetNoolite = "<h2><a href=\"/\">Home</a> / nooLite settings</h2><input type=\"hidden\" name=\"page\" value=\"noolite\"><table border=\"0\"><tr><td><b>Device ID:</b></td><td><input type=\"text\" name=\"deviceid\" value=\"{deviceid}\" size=\"40\" maxlength=\"32\">&nbsp;32 characters</td></tr><tr><td></td><td><input type=\"submit\" value=\"Save\"></td><td></td></tr></table>\r\n";
static const char *pageResetStarted = "<h1>Returning to normal mode...</h1>You can close this window now.\r\n";
static const char *pageSavedInfo = "<br><b style=\"color: green\">Settings saved!</b>\r\n";

static void ICACHE_FLASH_ATTR return_to_normal_mode_cb(void *arg)
{
	wifi_station_disconnect();
	wifi_set_opmode(STATION_MODE);
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("Restarting in STATION mode...\r\n");
	#endif
	system_restart();
}

#ifdef NOOLITE_LOGGING
static void ICACHE_FLASH_ATTR noolite_config_server_recon(void *arg, sint8 err)
{
	ets_uart_printf("noolite_config_server_recon\r\n");
}
#endif

#ifdef NOOLITE_LOGGING
static void ICACHE_FLASH_ATTR noolite_config_server_discon(void *arg)
{
    ets_uart_printf("noolite_config_server_discon\r\n");
}
#endif

static void ICACHE_FLASH_ATTR noolite_config_server_recv(void *arg, char *data, unsigned short len)
{
	struct espconn *ptrespconn = (struct espconn *)arg;

	if(os_strncmp(data, "GET ", 4) == 0)
	{
		char page[16];
		os_memset(page, 0, sizeof(page));
		noolite_config_server_get_key_val("page", sizeof(page), data, page);
		noolite_config_server_process_page(ptrespconn, page, data);
		return;
	}
	else
	{
		espconn_sent(ptrespconn, (uint8 *)http404Header, os_strlen(http404Header));
		killConn = 1;
		return;
	}
}

static void ICACHE_FLASH_ATTR noolite_config_server_process_page(struct espconn *ptrespconn, char *page, char *request)
{
	espconn_sent(ptrespconn, (uint8 *)http200Header, os_strlen(http200Header));
	espconn_sent(ptrespconn, (uint8 *)"\r\n", 2);
	espconn_sent(ptrespconn, (uint8 *)pageStart, os_strlen(pageStart));

	char save[2] = {'0', '\0'};
	char status[32] = "[status]";
	noolite_config_server_get_key_val("save", sizeof(save), request, save);
	os_memset(status, 0, sizeof(status));

	// wifi settings page
	if(os_strncmp(page, "wifi", 4) == 0 && strlen(page) == 4)
	{
		struct station_config stationConf;
		wifi_station_get_config(&stationConf);

		if(save[0] == '1')
		{
			os_memset(stationConf.ssid, 0, sizeof(stationConf.ssid));
			noolite_config_server_get_key_val("ssid", sizeof(stationConf.ssid), request, stationConf.ssid); //32
			char pass[64];
			os_memset(pass, 0, sizeof(pass));
			noolite_config_server_get_key_val("pass", sizeof(pass), request, pass); //64
			if(os_strncmp(pass, "***", 3) != 0)
			{
				os_memset(stationConf.password, 0, sizeof(stationConf.password));
				noolite_config_server_get_key_val("pass", sizeof(stationConf.password), request, stationConf.password); //64
			}

			wifi_station_disconnect();
			wifi_station_set_config(&stationConf);
			wifi_station_connect();
		}
		wifi_station_get_config(&stationConf); //remove?

		char *stream = (char *)pageSetWifi;
		char templateKey[16];
		os_memset(templateKey, 0, sizeof(templateKey));
		unsigned char templateKeyIdx;
		while(*stream)
		{
			if(*stream == '{')
			{
				templateKeyIdx = 0;
				stream++;
				while(*stream != '}')
				{
					templateKey[templateKeyIdx++] = *stream;
					stream++;
				}
				if(os_strncmp(templateKey, "ssid", 4) == 0)
				{
					espconn_sent(ptrespconn, (uint8 *)stationConf.ssid, os_strlen(stationConf.ssid));
				}
				else if(os_strncmp(templateKey, "pass", 4) == 0)
				{
					espconn_sent(ptrespconn, (uint8 *)stationConf.password, os_strlen(stationConf.password));
				}
				else if(os_strncmp(templateKey, "status", 6) == 0)
				{
					int x = wifi_station_get_connect_status();
					if (x == STATION_GOT_IP)
					{
						os_sprintf(status, "Connected");
					}
					else if(x == STATION_WRONG_PASSWORD)
					{
						os_sprintf(status, "Wrong Password");
					}
					else if(x == STATION_NO_AP_FOUND)
					{
						os_sprintf(status, "AP Not Found");
					}
					else if(x == STATION_CONNECT_FAIL)
					{
						os_sprintf(status, "Connect Failed");
					}
					else
					{
						os_sprintf(status, "Not Connected");
					}
					espconn_sent(ptrespconn, (uint8 *)status, os_strlen(status));
				}
			}
			else
			{
				espconn_sent(ptrespconn, (uint8 *)stream, 1);
			}
			stream++;
		}
		if(save[0] == '1')
		{
			espconn_sent(ptrespconn, (uint8 *)pageSavedInfo, os_strlen(pageSavedInfo));
		}
	}
	// nooLite settings page
	else if(os_strncmp(page, "noolite", 4) == 0 && strlen(page) == 7)
	{
		tSetup nooLiteSetup;

		if(save[0] == '1')
		{
			nooLiteSetup.SetupOk = SETUP_OK_KEY;

			// deviceid
			char deviceid[33];
			char *deviceidptr = deviceid;
			os_memset(deviceid, 0, sizeof(deviceid));
			noolite_config_server_get_key_val("deviceid", sizeof(deviceid), request, deviceid);
			unsigned char i = 0;
			while(i < 16)
			{
				char one[3] = {'0', '0', '\0'};
				one[0] = *deviceidptr;
				deviceidptr++;
				one[1] = *deviceidptr;
				deviceidptr++;
				nooLiteSetup.deviceId[i++] = strtol(one, NULL, 16);
			}

			save_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooLiteSetup, sizeof(tSetup));
		}

		load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooLiteSetup, sizeof(tSetup));

		char *stream = (char *)pageSetNoolite;
		char templateKey[16];
		os_memset(templateKey, 0, sizeof(templateKey));
		unsigned char templateKeyIdx;
		while(*stream)
		{
			if(*stream == '{')
			{
				templateKeyIdx = 0;
				stream++;
				while(*stream != '}')
				{
					templateKey[templateKeyIdx++] = *stream;
					stream++;
				}
				if(os_strncmp(templateKey, "deviceid", 6) == 0)
				{
					char deviceid[3];
					os_memset(deviceid, 0, sizeof(deviceid));
					char i;
					for(i=0; i<16; i++)
					{
						os_sprintf(deviceid, "%02x", nooLiteSetup.deviceId[i]);
						espconn_sent(ptrespconn, (uint8 *)deviceid, os_strlen(deviceid));
					}
				}
			}
			else
			{
				espconn_sent(ptrespconn, (uint8 *)stream, 1);
			}

			stream++;
		}
		if(save[0] == '1')
		{
			espconn_sent(ptrespconn, (uint8 *)pageSavedInfo, os_strlen(pageSavedInfo));
		}
	}
	else if(os_strncmp(page, "return", 3) == 0 && strlen(page) == 6)
	{
		espconn_sent(ptrespconn, (uint8 *)pageResetStarted, os_strlen(pageResetStarted));
		returnToNormalMode = 1;
	}
	else
	{
		espconn_sent(ptrespconn, (uint8 *)pageIndex, os_strlen(pageIndex));
	}
	espconn_sent(ptrespconn, (uint8 *)pageEnd, os_strlen(pageEnd));
	killConn = 1;
}

static unsigned char ICACHE_FLASH_ATTR noolite_config_server_get_key_val(char *key, unsigned char maxlen, char *str, char *retval)
{
	unsigned char found = 0;
	char *keyptr = key;
	char prev_char = '\0';
	*retval = '\0';

	while( *str && *str!='\r' && *str!='\n' && !found )
	{
		if(*str == *keyptr)
		{
			if(keyptr == key && !( prev_char == '?' || prev_char == '&' ) )
			{
				str++;
				continue;
			}
			keyptr++;
			if (*keyptr == '\0')
			{
				str++;
				keyptr = key;
				if (*str == '=')
				{
					found = 1;
				}
			}
		}
		else
		{
			keyptr = key;
		}
		prev_char = *str;
		str++;
	}
	if(found == 1)
	{
		found = 0;
		while( *str && *str!='\r' && *str!='\n' && *str!=' ' && *str!='&' && maxlen>0 )
		{
			*retval = *str;
			maxlen--;
			str++;
			retval++;
			found++;
		}
		*retval = '\0';
	}
	return found;
}

static void ICACHE_FLASH_ATTR noolite_config_server_sent(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;

	if (pesp_conn == NULL)
	{
		return;
	}

	if(killConn)
	{
		espconn_disconnect(pesp_conn);

		if(returnToNormalMode)
		{
			wifi_station_disconnect();
			wifi_set_opmode(STATION_MODE);
			os_timer_arm(&returnToNormalModeTimer, 1500, 0);
		}
	}
}

static void ICACHE_FLASH_ATTR noolite_config_server_connect(void *arg)
{
	struct espconn *pesp_conn = (struct espconn *)arg;

	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_config_server_connect\r\n");
	#endif

	espconn_regist_recvcb(pesp_conn, noolite_config_server_recv);
	espconn_regist_sentcb(pesp_conn, noolite_config_server_sent);
	#ifdef NOOLITE_LOGGING
	espconn_regist_reconcb(pesp_conn, noolite_config_server_recon);
	espconn_regist_disconcb(pesp_conn, noolite_config_server_discon);
	#endif
}

void ICACHE_FLASH_ATTR noolite_config_server_init()
{
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_config_server_init()\r\n");
	#endif

	os_timer_disarm(&returnToNormalModeTimer);
	os_timer_setfn(&returnToNormalModeTimer, return_to_normal_mode_cb, 0);

	esptcp.local_port = 80;
	esp_conn.type = ESPCONN_TCP;
	esp_conn.state = ESPCONN_NONE;
	esp_conn.proto.tcp = &esptcp;
	espconn_regist_connectcb(&esp_conn, noolite_config_server_connect);
	espconn_accept(&esp_conn);
}
