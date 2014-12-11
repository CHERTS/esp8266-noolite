#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

#include "noolite_control_server.h"
#include "flash_param.h"
#include "noolite_platform.h"

static struct espconn esp_conn;
static esp_tcp esptcp;
static unsigned char killConn;
// http headers
static const char *http404Header = "HTTP/1.0 404 Not Found\r\nServer: nooLite-Control-Server\r\nContent-Type: text/plain\r\n\r\nNot Found (or method not implemented).\r\n";
static const char *http200Header = "HTTP/1.0 200 OK\r\nServer: nooLite-Control-Server/0.1\r\nContent-Type: text/html\r\n";
// html page header and footer
static const char *pageStart = "<html><head><title>nooLite Control Panel</title><style>body{font-family: Arial}</style></head><body><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"set\" value=\"1\">\r\n";
static const char *pageEnd = "</form><hr>(c) 2014 by <a href=\"mailto:sleuthhound@gmail.com\" target=\"_blank\">Mikhail Grigorev</a>, <a href=\"http://programs74.ru\" target=\"_blank\">programs74.ru</a>\r\n</body></html>\r\n";
// html pages
static const char *pageIndex = "<h2>Welcome to nooLite Control Panel</h2>nooLite Control deviceID: {deviceid}<ul><li><a href=\"?page=devid\">Get deviceID</a></li><li><a href=\"?page=bind\">nooLite bind-unbind channel</a></li><li><a href=\"?page=control\">nooLite control</a></li></ul>\r\n";
static const char *pageSetBindChannel = "<h2><a href=\"/\">Home</a> / nooLite bind-unbind channel</h2><input type=\"hidden\" name=\"page\" value=\"bind\"><table border=\"0\"><tr><td><b>Channel #:</b></td><td><input type=\"text\" name=\"channel\" value=\"{channel}\" size=\"2\" maxlength=\"2\">&nbsp;0 .. 32</td></tr></tr><tr><td><b>Status:</b></td><td>{status}</td></tr><tr><td></td><td><input type=\"submit\" name = \"action\" value=\"Bind\"><input type=\"submit\" name = \"action\" value=\"Unbind\"></td></tr></table>\r\n";
static const char *pageSetNooliteControl = "<h2><a href=\"/\">Home</a> / nooLite control</h2><input type=\"hidden\" name=\"page\" value=\"control\"><table border=\"0\"><tr><td><b>Channel #:</b></td><td><input type=\"text\" name=\"channel\" value=\"{channel}\" size=\"2\" maxlength=\"2\">&nbsp;0 .. 32</td></tr><tr><td><b>Status:</b></td><td>{status}</td></tr><tr><td></td><td><input type=\"submit\" name = \"action\" value=\"On\"><input type=\"submit\" name = \"action\" value=\"Off\"></td></tr></table>\r\n";
static const char *pageCommandSent = "<br><b style=\"color: green\">Command sent!</b>\r\n";
static const char *pageDevID = "{deviceid}";
int recvOK = 0;

static void recvTask(os_event_t *events);

static int ICACHE_FLASH_ATTR noolite_sendCommand(unsigned char channel, unsigned char command, unsigned char data, unsigned char format)
{
	unsigned char buf[12];
	unsigned int i;
	int checkSum = 0;
	recvOK = 0;

	os_memset(buf, 0, 12);

	buf[0] = 85;
	buf[1] = 80;
	buf[2] = command;
	buf[3] = format;
	buf[5] = channel;
	buf[6] = data;

	for(i = 0; i < 10; i++) {
		checkSum+= buf[i];
	}

	buf[10] = lowByte(checkSum);
	buf[11] = 170;

	uart0_tx_buffer(&buf, 12);

	sleepms(300);

	recvOK = 1;
	#ifdef NOOLITE_LOGGING
	if(recvOK)
		ets_uart_printf("nooLite MT1132 send OK\r\n");
	#endif

	return recvOK;
}

#ifdef NOOLITE_LOGGING
static void ICACHE_FLASH_ATTR noolite_control_server_recon(void *arg, sint8 err)
{
    ets_uart_printf("noolite_control_server_recon\r\n");
}
#endif

#ifdef NOOLITE_LOGGING
static void ICACHE_FLASH_ATTR noolite_control_server_discon(void *arg)
{
    ets_uart_printf("noolite_control_server_discon\r\n");
}
#endif

static void ICACHE_FLASH_ATTR noolite_control_server_recv(void *arg, char *data, unsigned short len)
{
	struct espconn *ptrespconn = (struct espconn *)arg;

	if(os_strncmp(data, "GET ", 4) == 0)
	{
		char page[16];
		os_memset(page, 0, sizeof(page));
		noolite_control_server_get_key_val("page", sizeof(page), data, page);
		// Show deviceID page
		if(os_strncmp(page, "devid", 5) == 0 && strlen(page) == 5) {
			noolite_control_server_deviceid_page(ptrespconn, page, data);
		} else { // Other pages
			noolite_control_server_process_page(ptrespconn, page, data);
		}
		return;
	}
	else
	{
		espconn_sent(ptrespconn, (uint8 *)http404Header, os_strlen(http404Header));
		killConn = 1;
		return;
	}
}

static void ICACHE_FLASH_ATTR noolite_control_server_deviceid_page(struct espconn *ptrespconn, char *page, char *request)
{
	tSetup nooliteSetup;
	load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooliteSetup, sizeof(tSetup));

	char *stream = (char *)pageDevID;
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
				char i;
				for(i=0; i<16; i++)
				{
					os_sprintf(deviceid, "%02x", nooliteSetup.deviceId[i]);
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
	killConn = 1;
}

static void ICACHE_FLASH_ATTR noolite_control_server_process_page(struct espconn *ptrespconn, char *page, char *request)
{
	espconn_sent(ptrespconn, (uint8 *)http200Header, os_strlen(http200Header));
	espconn_sent(ptrespconn, (uint8 *)"\r\n", 2);
	espconn_sent(ptrespconn, (uint8 *)pageStart, os_strlen(pageStart));

	char set[2] = {'0', '\0'};
	noolite_control_server_get_key_val("set", sizeof(set), request, set);
	char channel_num[2] = "0";
	char status[32] = "[status]";
	char action[10] = "unbind";
	os_memset(channel_num, 0, sizeof(channel_num));
	os_memset(status, 0, sizeof(status));
	os_memset(action, 0, sizeof(action));

	// nooLite bind page
	if(os_strncmp(page, "bind", 4) == 0 && strlen(page) == 4)
	{
		if(set[0] == '1')
		{
			noolite_control_server_get_key_val("channel", sizeof(channel_num), request, channel_num);
			noolite_control_server_get_key_val("action", sizeof(action), request, action);
			// noolite_sendCommand(channel, command, data, format)
			// channel = 0 .. 32
			// command = 15 - bind
			// command = 9 - unbind
			// command = 2 - on
			// command = 0 - off
			if(atoi(channel_num) >= 0 && atoi(channel_num) <= 32) {
				if(os_strncmp(action, "Bind", 4) == 0 && strlen(action) == 4) {
					if(noolite_sendCommand(atoi(channel_num), 15, 0, 0)) {
						os_sprintf(status, "Bind OK");
					}
					else {
						os_sprintf(status, "Bind ERR");
					}
				} else if(os_strncmp(action, "Unbind", 6) == 0 && strlen(action) == 6) {
					if(noolite_sendCommand(atoi(channel_num), 9, 0, 0)) {
						os_sprintf(status, "Unbind OK");
					}
					else {
						os_sprintf(status, "Unbind ERR");
					}
				} else {
					os_sprintf(status, "Err");
				}
			}
			else {
				os_sprintf(status, "Err");
			}
		}

		char *stream = (char *)pageSetBindChannel;
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
				if(os_strncmp(templateKey, "channel", 7) == 0)
				{
					if(strlen(channel_num) == 0)
						os_sprintf(channel_num, "0");
					espconn_sent(ptrespconn, (uint8 *)channel_num, os_strlen(channel_num));
				}
				else if(os_strncmp(templateKey, "status", 6) == 0)
				{
					if(strlen(status) == 0) {
						os_sprintf(status, "[status]");
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
		if(set[0] == '1')
		{
			espconn_sent(ptrespconn, (uint8 *)pageCommandSent, os_strlen(pageCommandSent));
		}
	}
	// nooLite control page
	else if(os_strncmp(page, "control", 7) == 0 && strlen(page) == 7)
	{
		if(set[0] == '1')
		{
			noolite_control_server_get_key_val("channel", sizeof(channel_num), request, channel_num);
			noolite_control_server_get_key_val("action", sizeof(action), request, action);
			// noolite_sendCommand(channel, command, data, format)
			// channel = 0 .. 32
			// command = 15 - bind
			// command = 9 - unbind
			// command = 2 - on
			// command = 0 - off
			if(atoi(channel_num) >= 0 && atoi(channel_num) <= 32) {
				if(os_strncmp(action, "On", 2) == 0 && strlen(action) == 2) {
					if(noolite_sendCommand(atoi(channel_num), 2, 0, 0)) {
						os_sprintf(status, "On");
					}
					else {
						os_sprintf(status, "On ERR");
					}
				} else if(os_strncmp(action, "Off", 3) == 0 && strlen(action) == 3) {
					if (noolite_sendCommand(atoi(channel_num), 0, 0, 0)) {
						os_sprintf(status, "Off");
					}
					else {
						os_sprintf(status, "Off ERR");
					}
				} else {
					os_sprintf(status, "Err");
				}
			}
			else {
				os_sprintf(status, "Err");
			}
		}

		char *stream = (char *)pageSetNooliteControl;
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
				if(os_strncmp(templateKey, "channel", 7) == 0)
				{
					if(strlen(channel_num) == 0 )
						os_sprintf(channel_num, "0");
					espconn_sent(ptrespconn, (uint8 *)channel_num, os_strlen(channel_num));
				}
				else if(os_strncmp(templateKey, "status", 6) == 0)
				{
					if(strlen(status) == 0) {
						os_sprintf(status, "[status]");
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
		if(set[0] == '1')
		{
			espconn_sent(ptrespconn, (uint8 *)pageCommandSent, os_strlen(pageCommandSent));
		}
	}
	else
	{
		tSetup nooliteSetup;
		load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooliteSetup, sizeof(tSetup));

		char *stream = (char *)pageIndex;
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
					char i;
					for(i=0; i<16; i++)
					{
						os_sprintf(deviceid, "%02x", nooliteSetup.deviceId[i]);
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
	}
	espconn_sent(ptrespconn, (uint8 *)pageEnd, os_strlen(pageEnd));
	killConn = 1;
}

static unsigned char ICACHE_FLASH_ATTR noolite_control_server_get_key_val(char *key, unsigned char maxlen, char *str, char *retval)
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

static void ICACHE_FLASH_ATTR noolite_control_server_sent(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;

	if (pesp_conn == NULL)
	{
		return;
	}

	if(killConn)
	{
		espconn_disconnect(pesp_conn);
	}
}

static void ICACHE_FLASH_ATTR noolite_control_server_connect(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;

	#ifdef NOOLITE_LOGGING
		ets_uart_printf("noolite_control_server_connect\r\n");
	#endif

    espconn_regist_recvcb(pesp_conn, noolite_control_server_recv);
    espconn_regist_sentcb(pesp_conn, noolite_control_server_sent);
	#ifdef NOOLITE_LOGGING
    espconn_regist_reconcb(pesp_conn, noolite_control_server_recon);
    espconn_regist_disconcb(pesp_conn, noolite_control_server_discon);
	#endif
}

void ICACHE_FLASH_ATTR noolite_control_server_init()
{
	#ifdef NOOLITE_LOGGING
		ets_uart_printf("noolite_control_server_init()\r\n");
	#endif
    esptcp.local_port = 80;

    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    espconn_regist_connectcb(&esp_conn, noolite_control_server_connect);

    espconn_accept(&esp_conn);

    os_event_t *recvTaskQueue = (os_event_t *)os_malloc(sizeof(os_event_t) * recvTaskQueueLen);
	system_os_task(recvTask, recvTaskPrio, recvTaskQueue, recvTaskQueueLen);
}

static void ICACHE_FLASH_ATTR recvTask(os_event_t *events)
{
	switch (events->sig) {
		case 0:
			#ifdef NOOLITE_LOGGING
				ets_uart_printf("nooLite MT1132 sent OK\r\n");
			#endif
			recvOK = 1;
			break;
		default:
			break;
	}
}
