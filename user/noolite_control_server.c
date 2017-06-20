#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

#include "noolite_control_server.h"
#include "flash_param.h"
#include "noolite_platform.h"
#include "utils.h"

//Private data for http connection
struct HttpdPriv {
	char *sendBuff;
	int sendBuffLen;
};

//Connection pool
static HttpdPriv connPrivData[MAX_CONN];
static HttpdConnData connControlData[MAX_CONN];

static struct espconn esp_conn;
static esp_tcp esptcp;
static unsigned char killConn;
// html page header and footer
static const char *pageStart = "<html><head><title>ESPOOLITE Control Panel</title><style>body{font-family: Arial};#dva:{width:5em;}</style></head><body><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"set\" value=\"1\">\r\n";
#ifdef NO_COPYRIGHT
static const char *pageEnd = "</form><hr>ESPOOLITE v{version} (c) 2014-2016</body></html>\r\n";
#endif
#ifndef NO_COPYRIGHT
static const char *pageEnd = "</form><hr>ESPOOLITE v{version} (c) 2014-2016 by <a href=\"mailto:sleuthhound@gmail.com\" target=\"_blank\">Mikhail Grigorev</a>, <a href=\"http://programs74.ru\" target=\"_blank\">programs74.ru</a></body></html>\r\n";
#endif
// html pages
static const char *pageIndex = "<h2>ESPOOLITE Control Panel</h2>DeviceID: {deviceid}<ul><li><a href=\"?page=devid\">Get DeviceID</a></li><li><a href=\"?page=bind\">Bind and Unbind device</a></li><li><a href=\"?page=control\">Base control</a></li><li><a href=\"?page=customcontrol\">Custom control</a></li></ul>\r\n";
static const char *pageSetBindChannel = "<h2><a href=\"/\">Home</a> / Bind and Unbind device</h2><input type=\"hidden\" name=\"page\" value=\"bind\"><table border=\"0\"><tr><td><b>Channel:</b></td><td><input type=\"number\" name=\"channel\" value=\"{channel}\" min=\"0\" max=\"31\" step=\"1\" id=\"dva\" required autofocus>&nbsp;0 .. 31</td></tr></tr><tr><td><b>Status:</b></td><td>{status}</td></tr><tr><td></td><td><input type=\"submit\" name=\"action\" value=\"Bind\"><input type=\"submit\" name=\"action\" value=\"Unbind\"></td></tr></table>\r\n";
static const char *pageSetNooliteControl = "<h2><a href=\"/\">Home</a> / Base control</h2><input type=\"hidden\" name=\"page\" value=\"control\"><table border=\"0\"><tr><td><b>Channel:</b></td><td><input type=\"number\" name=\"channel\" value=\"{channel}\" min=\"0\" max=\"31\" step=\"1\" id=\"dva\" required autofocus>&nbsp;0 .. 31</td></tr><tr><td><b>Status:</b></td><td>{status}</td></tr><tr><td></td><td><input type=\"submit\" name=\"action\" value=\"On\"><input type=\"submit\" name=\"action\" value=\"Off\"></td></tr></table>\r\n";
static const char *pageSetNooliteCustomControl = "<h2><a href=\"/\">Home</a> / Custom control</h2><input type=\"hidden\" name=\"page\" value=\"customcontrol\"><table border=\"0\"><tr><td><b>Channel:</b></td><td><input type=\"number\" name=\"channel\" value=\"{channel}\" min=\"0\" max=\"31\" step=\"1\" id=\"dva\" required autofocus>&nbsp;0 .. 31</td></tr><tr><td><b>Command:</b></td><td><input type=\"number\" name=\"command\" value=\"{command}\" min=\"0\" max=\"19\" step=\"1\" id=\"dva\" required>&nbsp;0 .. 10, 15 .. 19</td></tr><tr><td><b>Format:</b></td><td><input type=\"number\" name=\"format\" value=\"{format}\" min=\"0\" max=\"3\" step=\"1\" id=\"dva\" required>&nbsp;0, 1, 3</td></tr><tr><td><b>Data 0:</b></td><td><input type=\"number\" name=\"data0\" value=\"{data0}\" min=\"0\" max=\"255\" step=\"1\" id=\"dva\" required>&nbsp;0 .. 255</td></tr><tr><td><b>Data 1:</b></td><td><input type=\"number\" name=\"data1\" value=\"{data1}\" min=\"0\" max=\"255\" step=\"1\" id=\"dva\" required>&nbsp;0 .. 255</td></tr><tr><td><b>Data 2:</b></td><td><input type=\"number\" name=\"data2\" value=\"{data2}\" min=\"0\" max=\"255\" step=\"1\" id=\"dva\" required>&nbsp;0 .. 255</td></tr><tr><td><b>Status:</b></td><td>{status}</td></tr><tr><td></td><td><input type=\"submit\" name = \"action\" value=\"Send\"></td></tr><tr><td colspan=2>*<i>Data format see <a href='http://goo.gl/gyHnNX' title='Data format'>here</a>, page 5-7</i></td></tr></table>\r\n";
static const char *pageCommandSent = "<br><b style=\"color: green\">Command sent!</b>\r\n";
static const char *pageDevID = "{deviceid}";
int recvOK = 0;

static void recvTask(os_event_t *events);

// noolite_sendCommand(channel, command, format, data0, data1, data2, data3)
// Full docs: http://www.noo.com.by/assets/files/PDF/MT1132.pdf
// channel = 0 .. 31
// command = 19 - change speed in working mode (переключить скорость эффекта в режиме работы)
// command = 18 - change working mode (переключить режим работы)
// command = 17 - change color (переключить цвет)
// command = 16 - on color random (включить плавный перебор цвета, выключается командой 10)
// command = 15 - bind (сообщить исполнительному устройству, что управляющее устройство хочет записать свой адрес в его память (привязка))
// command = 10 - stop (остановить регулировку)
// command = 9  - unbind (запустить процедуру стирания адреса управляющего устройства из памяти исполнительного устройства (отвязка))
// command = 8  - record scene (записать сценарий)
// command = 7  - run scene (вызвать записанный сценарий)
// command = 6  - установить заданную в "Байт данных 0" (data0) яркость, установить заданную в "Байт данных 0, 1, 2" (data0,1,2) яркость*
// command = 5  - slow on/off (запустить плавное изменение яркости в обратном направлении)
// command = 4  - on/off (включить или выключить нагрузку)
// command = 3  - slow on (запустить плавное повышение яркости)
// command = 2  - on (включить нагрузку)
// command = 1  - slow off (запустить плавное понижение яркости)
// command = 0  - off (выключить нагрузку)
// format	- Формат. При передаче команды со значением 6 - значение Формат=1 (яркость – "Байт данных 0" (data0)) или
//		  Формат=3 (яркость на каждый канал независимо - "Байт данных 0,1,2 (data0,1,2)).
//		  При передаче остальных команд без данных – значение Формат=0
// data0	- Байт данных 0. При передаче команды со значением=6 и Формат=1 в данном байте содержится информация о яркости, которая будет
//		  установлена (значение в диапазоне 35…155). При значении 0 – свет выключится, при значении больше 155 – свет включится на максимальную яркость.
//		  * При передаче команды со значением=6 и Формат=3 в данном байте содержится информация о яркости, которая будет установлена (значение в диапазоне 0…255) на канал R.
// data1	- Байт данных 1. *При передаче команды со значением=6 и Формат=3 в данном байте содержится информация о яркости, которая будет установлена (значение в диапазоне 0…255) на канал G.
// data2	- Байт данных 2. *При передаче команды со значением=6 и Формат=3 в данном байте содержится информация о яркости, которая будет установлена (значение в диапазоне 0…255) на канал B.
// data3	- Байт данных 3. Значение=0
static int ICACHE_FLASH_ATTR noolite_sendCommand(unsigned char channel, unsigned char command, unsigned char format, unsigned char data0, unsigned char data1, unsigned char data2, unsigned char data3)
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
	buf[6] = data0;
	buf[7] = data1;
	buf[8] = data2;
	buf[9] = data3;

	for(i = 0; i < 10; i++) {
		checkSum+= buf[i];
	}

	buf[10] = lowByte(checkSum);
	buf[11] = 170;

	uart0_tx_buffer(&buf, 12);

	sleepms(300);

	recvOK = 1;
	if(recvOK)
		ESPOOLITE_LOGGING("nooLite MT1132 send OK\r\n");

	return recvOK;
}

static void ICACHE_FLASH_ATTR noolite_control_server_recon(void *arg, sint8 err)
{
    ESPOOLITE_LOGGING("noolite_control_server_recon\r\n");
}

static void ICACHE_FLASH_ATTR noolite_control_server_discon(void *arg)
{
	ESPOOLITE_LOGGING("noolite_control_server_discon\r\n");
	//Just look at all the sockets and kill the slot if needed.
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connControlData[i].conn!=NULL) {
			//Why the >=ESPCONN_CLOSE and not ==? Well, seems the stack sometimes de-allocates
			//espconns under our noses, especially when connections are interrupted. The memory
			//is then used for something else, and we can use that to capture *most* of the
			//disconnect cases.
			if (connControlData[i].conn->state==ESPCONN_NONE || connControlData[i].conn->state>=ESPCONN_CLOSE) {
				connControlData[i].conn=NULL;
				control_httpdRetireConn(&connControlData[i]);
			}
		}
	}
}

static void ICACHE_FLASH_ATTR noolite_control_server_recv(void *arg, char *data, unsigned short len)
{
	ESPOOLITE_LOGGING("noolite_control_server_recv\r\n");

	char sendBuff[MAX_SENDBUFF_LEN];
	HttpdConnData *conn = control_httpdFindConnData(arg);

	if (conn==NULL)
		return;
	conn->priv->sendBuff = sendBuff;
	conn->priv->sendBuffLen = 0;

	if(os_strncmp(data, "GET ", 4) == 0) {
		ESPOOLITE_LOGGING("noolite_control_server_recv: get\r\n");
		char page[16];
		os_memset(page, 0, sizeof(page));
		noolite_control_server_get_key_val("page", sizeof(page), data, page);
		// Show deviceID page
		if(os_strncmp(page, "devid", 5) == 0 && strlen(page) == 5) {
			noolite_control_server_deviceid_page(conn, page, data);
		} else { // Other pages
			noolite_control_server_process_page(conn, page, data);
		}
	} else {
		const char *notfound="404 Not Found (or method not implemented).";
		control_httpdStartResponse(conn, 404);
		control_httpdHeader(conn, "Content-Type", "text/plain");
		control_httpdEndHeaders(conn);
		control_httpdSend(conn, notfound, -1);
		killConn = 1;
	}
	control_xmitSendBuff(conn);
	return;
}

static void ICACHE_FLASH_ATTR noolite_control_server_deviceid_page(struct HttpdConnData *conn, char *page, char *request)
{
	tSetup nooliteSetup;
	char buff[1024];
	char html_buff[1024];
	int len;
	char *result;
	load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooliteSetup, sizeof(tSetup));
	bin2strhex((char *)nooliteSetup.deviceId, sizeof(nooliteSetup.deviceId), &result);
	os_sprintf(html_buff, "%s", str_replace(pageDevID, "{deviceid}", result));
	os_free(result);
	len = os_sprintf(buff, html_buff);
	control_httpdSend(conn, buff, len);
	killConn = 1;
}

static void ICACHE_FLASH_ATTR noolite_control_server_process_page(struct HttpdConnData *conn, char *page, char *request)
{
	char buff[2048];
	char html_buff[2048];
	char version_buff[10];
	int len,n;
	int value_parse_status = 0;

	ESPOOLITE_LOGGING("noolite_control_server_process_page: start\r\n");

	control_httpdStartResponse(conn, 200);
	control_httpdHeader(conn, "Content-Type", "text/html");
	control_httpdEndHeaders(conn);

	// page header
	len = os_sprintf(buff, pageStart);
	if(!control_httpdSend(conn, buff, len)) {
		ESPOOLITE_LOGGING("Error httpdSend: pageStart out-of-memory\r\n");
	}

	char set[2] = {'0', '\0'};
	noolite_control_server_get_key_val("set", sizeof(set), request, set);
	char channel_num[10] = "0";
	char command[10] = "0";
	char format[10] = "0";
	char data0[10] = "0";
	char data1[10] = "0";
	char data2[10] = "0";
	char status[64] = "[status]";
	char action[10] = "unbind";
	os_memset(channel_num, 0, sizeof(channel_num));
	os_memset(status, 0, sizeof(status));
	os_memset(action, 0, sizeof(action));
	os_memset(command, 0, sizeof(command));
	os_memset(format, 0, sizeof(format));
	os_memset(data0, 0, sizeof(data0));
	os_memset(data1, 0, sizeof(data1));
	os_memset(data2, 0, sizeof(data2));

	ESPOOLITE_LOGGING("noolite_control_server_process_page: page=%s\n", page);

	// nooLite bind page
	if(os_strncmp(page, "bind", 4) == 0 && strlen(page) == 4)
	{
		if(set[0] == '1')
		{
			noolite_control_server_get_key_val("channel", sizeof(channel_num), request, channel_num);
			noolite_control_server_get_key_val("action", sizeof(action), request, action);
			if(isItNum(channel_num, n)) {
				if(atoi(channel_num) >= 0 && atoi(channel_num) <= 31) {
					if(os_strncmp(action, "Bind", 4) == 0 && strlen(action) == 4) {
						if(noolite_sendCommand(atoi(channel_num), 15, 0, 0, 0, 0, 0)) {
							os_sprintf(status, "Bind OK");
						} else {
							os_sprintf(status, "Bind ERR");
						}
					} else if(os_strncmp(action, "Unbind", 6) == 0 && strlen(action) == 6) {
						if(noolite_sendCommand(atoi(channel_num), 9, 0, 0, 0, 0, 0)) {
							os_sprintf(status, "Unbind OK");
						} else {
							os_sprintf(status, "Unbind ERR");
						}
					} else {
						os_sprintf(status, "Err");
					}
				} else {
					os_sprintf(status, "Err");
				}
			} else {
				os_sprintf(status, "Channel number incorrect");
			}
		}

		if(strlen(channel_num) == 0 || !isItNum(channel_num, n) || atoi(channel_num) > 31)
			os_sprintf(channel_num, "0");
		os_sprintf(html_buff, "%s", str_replace(pageSetBindChannel, "{channel}", channel_num));
		if(strlen(status) == 0)
			os_sprintf(status, "[status]");
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{status}", status));

		if(set[0] == '1')
		{
			char buff_saved[512];
			os_sprintf(buff_saved, "%s%s", html_buff, pageCommandSent);
			len = os_sprintf(buff, buff_saved);
			control_httpdSend(conn, buff, len);
		} else {
			len = os_sprintf(buff, html_buff);
			control_httpdSend(conn, buff, len);
		}
	}
	// nooLite control page
	else if(os_strncmp(page, "control", 7) == 0 && strlen(page) == 7)
	{
		if(set[0] == '1')
		{
			noolite_control_server_get_key_val("channel", sizeof(channel_num), request, channel_num);
			noolite_control_server_get_key_val("action", sizeof(action), request, action);
			if(isItNum(channel_num, n)) {
				if(atoi(channel_num) >= 0 && atoi(channel_num) <= 31) {
					if(os_strncmp(action, "On", 2) == 0 && strlen(action) == 2) {
						if(noolite_sendCommand(atoi(channel_num), 2, 0, 0, 0, 0, 0)) {
							os_sprintf(status, "On");
						} else {
							os_sprintf(status, "On ERR");
						}
					} else if(os_strncmp(action, "Off", 3) == 0 && strlen(action) == 3) {
						if (noolite_sendCommand(atoi(channel_num), 0, 0, 0, 0, 0, 0)) {
							os_sprintf(status, "Off");
						} else {
							os_sprintf(status, "Off ERR");
						}
					} else {
						os_sprintf(status, "Err");
					}
				} else {
					os_sprintf(status, "Err");
				}
			} else {
				os_sprintf(status, "Channel number incorrect");
			}
		}

		if(strlen(channel_num) == 0 || !isItNum(channel_num, n) || atoi(channel_num) > 31)
			os_sprintf(channel_num, "0");
		os_sprintf(html_buff, "%s", str_replace(pageSetNooliteControl, "{channel}", channel_num));
		if(strlen(status) == 0)
			os_sprintf(status, "[status]");
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{status}", status));

		if(set[0] == '1')
		{
			char buff_saved[512];
			os_sprintf(buff_saved, "%s%s", html_buff, pageCommandSent);
			len = os_sprintf(buff, buff_saved);
			control_httpdSend(conn, buff, len);
		} else {
			len = os_sprintf(buff, html_buff);
			control_httpdSend(conn, buff, len);
		}
	}
	// nooLite custom control page
	else if(os_strncmp(page, "customcontrol", 13) == 0 && strlen(page) == 13)
	{
		if(set[0] == '1')
		{
			noolite_control_server_get_key_val("channel", sizeof(channel_num), request, channel_num);
			noolite_control_server_get_key_val("command", sizeof(command), request, command);
			noolite_control_server_get_key_val("format", sizeof(format), request, format);
			noolite_control_server_get_key_val("data0", sizeof(data0), request, data0);
			noolite_control_server_get_key_val("data1", sizeof(data1), request, data1);
			noolite_control_server_get_key_val("data2", sizeof(data2), request, data2);
			noolite_control_server_get_key_val("action", sizeof(action), request, action);
			value_parse_status = 0;
			char err_buff_saved[1024];
			if(os_strncmp(action, "Send", 4) == 0 && strlen(action) == 4) {
				if(isItNum(channel_num, n)) {
					if(atoi(channel_num) >= 0 && atoi(channel_num) <= 31) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						os_sprintf(status, "Channel number incorrect");
					}
				} else {
					value_parse_status = 0;
					os_sprintf(status, "Channel number incorrect");
				}
				// command
				if(isItNum(command, n)) {
					if((atoi(command) >= 0 && atoi(command) <= 10) || (atoi(command) >= 15 && atoi(command) <= 19)) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						if(strlen(status) > 8) {
							os_sprintf(err_buff_saved, "%s and %s", status, "Command number incorrect");
							os_sprintf(status, err_buff_saved);
						} else {
							os_sprintf(status, "Command number incorrect");
						}
					}
				} else {
					if(strlen(status) > 8) {
						os_sprintf(err_buff_saved, "%s and %s", status, "Command number incorrect");
						os_sprintf(status, err_buff_saved);
					} else {
						os_sprintf(status, "Command number incorrect");
					}
				}
				// format
				if(isItNum(format, n)) {
					if(atoi(format) >= 0 && atoi(format) <= 3) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						if(strlen(status) > 8) {
							os_sprintf(err_buff_saved, "%s and %s", status, "Format number incorrect");
							os_sprintf(status, err_buff_saved);
						} else {
							os_sprintf(status, "Format number incorrect");
						}
					}
				} else {
					value_parse_status = 0;
					if(strlen(status) > 8) {
						os_sprintf(err_buff_saved, "%s and %s", status, "Format number incorrect");
						os_sprintf(status, err_buff_saved);
					} else {
						os_sprintf(status, "Format number incorrect");
					}
				}
				// data0
				if(isItNum(data0, n)) {
					if(atoi(data0) >= 0 && atoi(data0) <= 255) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						if(strlen(status) > 8) {
							os_sprintf(err_buff_saved, "%s and %s", status, "Data0 incorrect");
							os_sprintf(status, err_buff_saved);
						} else {
							os_sprintf(status, "Data0 incorrect");
						}
					}
				} else {
					value_parse_status = 0;
					if(strlen(status) > 8) {
						os_sprintf(err_buff_saved, "%s and %s", status, "Data0 incorrect");
						os_sprintf(status, err_buff_saved);
					} else {
						os_sprintf(status, "Data0 incorrect");
					}
				}
				// data1
				if(isItNum(data1, n)) {
					if(atoi(data1) >= 0 && atoi(data1) <= 255) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						if(strlen(status) > 8) {
							os_sprintf(err_buff_saved, "%s and %s", status, "Data1 incorrect");
							os_sprintf(status, err_buff_saved);
						} else {
							os_sprintf(status, "Data1 incorrect");
						}
					}
				} else {
					value_parse_status = 0;
					if(strlen(status) > 8) {
						os_sprintf(err_buff_saved, "%s and %s", status, "Data1 incorrect");
						os_sprintf(status, err_buff_saved);
					} else {
						os_sprintf(status, "Data1 incorrect");
					}
				}
				// data2
				if(isItNum(data2, n)) {
					if(atoi(data2) >= 0 && atoi(data2) <= 255) {
						value_parse_status++;
					} else {
						value_parse_status = 0;
						if(strlen(status) > 8) {
							os_sprintf(err_buff_saved, "%s and %s", status, "Data2 incorrect");
							os_sprintf(status, err_buff_saved);
						} else {
							os_sprintf(status, "Data2 incorrect");
						}
					}
				} else {
					value_parse_status = 0;
					if(strlen(status) > 8) {
						os_sprintf(err_buff_saved, "%s and %s", status, "Data2 incorrect");
						os_sprintf(status, err_buff_saved);
					} else {
						os_sprintf(status, "Data2 incorrect");
					}
				}
				// summary error check
				if(value_parse_status == 6) {
					if(noolite_sendCommand(atoi(channel_num), atoi(command), atoi(format), atoi(data0), atoi(data1), atoi(data2), 0)) {
						os_sprintf(status, "Sent");
					} else {
						os_sprintf(status, "Err: Command not sent");
					}
				} else {
					os_sprintf(err_buff_saved, "Err: %s", status);
					os_sprintf(status, err_buff_saved);
				}
			} else {
				os_sprintf(status, "Err: Send value not set");
			}
			ESPOOLITE_LOGGING("Status: %d, noolite_sendCommand(%s,%s,%s,%s,%s,%s,0)\r\n",value_parse_status,channel_num,command,format,data0,data1,data2);
		}

		if(strlen(channel_num) == 0 || !isItNum(channel_num, n) || atoi(channel_num) > 31)
			os_sprintf(channel_num, "0");
		if(strlen(command) == 0 || !isItNum(command, n) || atoi(command) > 19)
			os_sprintf(command, "0");
		if(strlen(format) == 0 || !isItNum(format, n) || atoi(format) > 3)
			os_sprintf(format, "0");
		if(strlen(data0) == 0 || !isItNum(data0, n) || atoi(data0) > 255)
			os_sprintf(data0, "0");
		if(strlen(data1) == 0 || !isItNum(data1, n) || atoi(data1) > 255)
			os_sprintf(data1, "0");
		if(strlen(data2) == 0 || !isItNum(data2, n) || atoi(data2) > 255)
			os_sprintf(data2, "0");
		os_sprintf(html_buff, "%s", str_replace(pageSetNooliteCustomControl, "{channel}", channel_num));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{command}", command));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{format}", format));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{data0}", data0));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{data1}", data1));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{data2}", data2));

		if(strlen(status) == 0)
			os_sprintf(status, "[status]");
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{status}", status));

		if(set[0] == '1')
		{
			char buff_saved[1024];
			os_sprintf(buff_saved, "%s%s", html_buff, pageCommandSent);
			len = os_sprintf(buff, buff_saved);
			control_httpdSend(conn, buff, len);
		} else {
			len = os_sprintf(buff, html_buff);
			control_httpdSend(conn, buff, len);
		}
	} else {
		ESPOOLITE_LOGGING("noolite_control_server_process_page: pageIndex\r\n");
		tSetup nooliteSetup;
		char *result;
		load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooliteSetup, sizeof(tSetup));
		bin2strhex((char *)nooliteSetup.deviceId, sizeof(nooliteSetup.deviceId), &result);
		os_sprintf(html_buff, "%s", str_replace(pageIndex, "{deviceid}", result));
		os_free(result);
		len = os_sprintf(buff, html_buff);
		if(!control_httpdSend(conn, buff, len)) {
			ESPOOLITE_LOGGING("Error httpdSend: pageIndex out-of-memory\r\n");
		}
	}

	// page footer
	//len = os_sprintf(buff, pageEnd);
	os_sprintf(version_buff, "%s", ESPOOLITE_VERSION);
	len = os_sprintf(buff, "%s", str_replace(pageEnd, "{version}", version_buff));
	if(!control_httpdSend(conn, buff, len)) {
		ESPOOLITE_LOGGING("Error httpdSend: pageEnd out-of-memory\r\n");
	}
	killConn = 1;
	ESPOOLITE_LOGGING("noolite_control_server_process_page: end\r\n");
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
	HttpdConnData *conn = control_httpdFindConnData(arg);

	if (conn==NULL) return;

	if(killConn)
	{
		espconn_disconnect(conn->conn);
	}
}

static void ICACHE_FLASH_ATTR noolite_control_server_connect(void *arg)
{
	ESPOOLITE_LOGGING("noolite_control_server_connect\r\n");

	struct espconn *conn=arg;
	int i;
	//Find empty conndata in pool
	for (i=0; i<MAX_CONN; i++)
		if (connControlData[i].conn==NULL) break;

	ESPOOLITE_LOGGING("Con req, conn=%p, pool slot %d\n", conn, i);

	if (i==MAX_CONN) {
		ESPOOLITE_LOGGING("Conn pool overflow!\r\n");
		espconn_disconnect(conn);
		return;
	}
	connControlData[i].conn = conn;
	connControlData[i].postLen = 0;
	connControlData[i].priv = &connPrivData[i];

	espconn_regist_recvcb(conn, noolite_control_server_recv);
	espconn_regist_sentcb(conn, noolite_control_server_sent);
	espconn_regist_reconcb(conn, noolite_control_server_recon);
	espconn_regist_disconcb(conn, noolite_control_server_discon);
}

void ICACHE_FLASH_ATTR noolite_control_server_init()
{
	int i;

	ESPOOLITE_LOGGING("noolite_control_server_init()\r\n");

	for (i=0; i<MAX_CONN; i++) {
		connControlData[i].conn=NULL;
	}

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
			ESPOOLITE_LOGGING("nooLite MT1132 sent OK\r\n");
			recvOK = 1;
			break;
		default:
			break;
	}
}

//Looks up the connData info for a specific esp connection
static ICACHE_FLASH_ATTR HttpdConnData ICACHE_FLASH_ATTR *control_httpdFindConnData(void *arg) {
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connControlData[i].conn==(struct espconn *)arg)
			return &connControlData[i];
	}
	ESPOOLITE_LOGGING("FindConnData: Couldn't find connection for %p\n", arg);
	return NULL; //WtF?
}

//Add data to the send buffer. len is the length of the data. If len is -1
//the data is seen as a C-string.
//Returns 1 for success, 0 for out-of-memory.
int ICACHE_FLASH_ATTR control_httpdSend(HttpdConnData *conn, const char *data, int len) {
	if (len<0)
		len = strlen(data);
	if (conn->priv->sendBuffLen+len > MAX_SENDBUFF_LEN)
		return 0;
	os_memcpy(conn->priv->sendBuff+conn->priv->sendBuffLen, data, len);
	conn->priv->sendBuffLen += len;
	return 1;
}

//Helper function to send any data in conn->priv->sendBuff
static ICACHE_FLASH_ATTR void ICACHE_FLASH_ATTR control_xmitSendBuff(HttpdConnData *conn) {
	if (conn->priv->sendBuffLen != 0) {
		ESPOOLITE_LOGGING("xmitSendBuff\r\n");
		espconn_sent(conn->conn, (uint8_t*)conn->priv->sendBuff, conn->priv->sendBuffLen);
		conn->priv->sendBuffLen = 0;
	}
}

//Start the response headers.
void ICACHE_FLASH_ATTR control_httpdStartResponse(HttpdConnData *conn, int code) {
	char buff[128];
	int l;
	l = os_sprintf(buff, "HTTP/1.0 %d OK\r\nServer: ESPOOLITE-Control-Server/0.1\r\n", code);
	control_httpdSend(conn, buff, l);
}

//Send a http header.
void ICACHE_FLASH_ATTR control_httpdHeader(HttpdConnData *conn, const char *field, const char *val) {
	char buff[256];
	int l;
	l = os_sprintf(buff, "%s: %s\r\n", field, val);
	control_httpdSend(conn, buff, l);
}

//Finish the headers.
void ICACHE_FLASH_ATTR control_httpdEndHeaders(HttpdConnData *conn) {
	control_httpdSend(conn, "\r\n", -1);
}

static void ICACHE_FLASH_ATTR control_httpdRetireConn(HttpdConnData *conn) {
	conn->conn=NULL;
}
