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

static void ICACHE_FLASH_ATTR noolite_control_server_discon(void *arg)
{
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_control_server_discon\r\n");
	#endif
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
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_control_server_recv\r\n");
	#endif

	char sendBuff[MAX_SENDBUFF_LEN];
	HttpdConnData *conn = control_httpdFindConnData(arg);

	if (conn==NULL)
		return;
	conn->priv->sendBuff = sendBuff;
	conn->priv->sendBuffLen = 0;

	if(os_strncmp(data, "GET ", 4) == 0)
	{
		#ifdef NOOLITE_LOGGING
		ets_uart_printf("noolite_control_server_recv: get\r\n");
		#endif
		char page[16];
		os_memset(page, 0, sizeof(page));
		noolite_control_server_get_key_val("page", sizeof(page), data, page);
		// Show deviceID page
		if(os_strncmp(page, "devid", 5) == 0 && strlen(page) == 5) {
			noolite_control_server_deviceid_page(conn, page, data);
		} else { // Other pages
			noolite_control_server_process_page(conn, page, data);
		}
	}
	else
	{
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
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_control_server_process_page: start\r\n");
	#endif

	control_httpdStartResponse(conn, 200);
	control_httpdHeader(conn, "Content-Type", "text/html");
	control_httpdEndHeaders(conn);
	// page header
	char buff[1024];
	char html_buff[1024];
	int len;
	len = os_sprintf(buff, pageStart);
	if(!control_httpdSend(conn, buff, len)) {
		#ifdef NOOLITE_LOGGING
		ets_uart_printf("Error httpdSend: pageStart out-of-memory\r\n");
		#endif
	}

	char set[2] = {'0', '\0'};
	noolite_control_server_get_key_val("set", sizeof(set), request, set);
	char channel_num[2] = "0";
	char status[32] = "[status]";
	char action[10] = "unbind";
	os_memset(channel_num, 0, sizeof(channel_num));
	os_memset(status, 0, sizeof(status));
	os_memset(action, 0, sizeof(action));

	#ifdef NOOLITE_LOGGING
	char temp[100];
	os_sprintf(temp, "noolite_control_server_process_page: page=%s\n", page);
	ets_uart_printf(temp);
	#endif

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

		if(strlen(channel_num) == 0)
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


		if(strlen(channel_num) == 0)
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
	else
	{
		#ifdef NOOLITE_LOGGING
		os_printf("noolite_control_server_process_page: pageIndex\r\n");
		#endif
		tSetup nooliteSetup;
		char *result;
		load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooliteSetup, sizeof(tSetup));
		bin2strhex((char *)nooliteSetup.deviceId, sizeof(nooliteSetup.deviceId), &result);
		os_sprintf(html_buff, "%s", str_replace(pageIndex, "{deviceid}", result));
		os_free(result);
		len = os_sprintf(buff, html_buff);
		if(!control_httpdSend(conn, buff, len)){
			#ifdef NOOLITE_LOGGING
				os_printf("Error httpdSend: pageIndex out-of-memory\r\n");
			#endif
		}
	}
	len = os_sprintf(buff, pageEnd);
	if(!control_httpdSend(conn, buff, len)){
		#ifdef NOOLITE_LOGGING
			os_printf("Error httpdSend: pageEnd out-of-memory\r\n");
		#endif
	}
	killConn = 1;
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_control_server_process_page: end\r\n");
	#endif
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
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("noolite_control_server_connect\r\n");
	#endif

	struct espconn *conn=arg;
	int i;
	//Find empty conndata in pool
	for (i=0; i<MAX_CONN; i++)
		if (connControlData[i].conn==NULL) break;
	#ifdef NOOLITE_LOGGING
	char temp[100];
	os_sprintf(temp, "Con req, conn=%p, pool slot %d\n", conn, i);
	ets_uart_printf(temp);
	#endif
	if (i==MAX_CONN) {
		#ifdef NOOLITE_LOGGING
		ets_uart_printf("Conn pool overflow!\r\n");
		#endif
		espconn_disconnect(conn);
		return;
	}
	connControlData[i].conn = conn;
	connControlData[i].postLen = 0;
	connControlData[i].priv = &connPrivData[i];

    espconn_regist_recvcb(conn, noolite_control_server_recv);
    espconn_regist_sentcb(conn, noolite_control_server_sent);
	#ifdef NOOLITE_LOGGING
    espconn_regist_reconcb(conn, noolite_control_server_recon);
	#endif
    espconn_regist_disconcb(conn, noolite_control_server_discon);
}

void ICACHE_FLASH_ATTR noolite_control_server_init()
{
	int i;

	#ifdef NOOLITE_LOGGING
		ets_uart_printf("noolite_control_server_init()\r\n");
	#endif

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
			#ifdef NOOLITE_LOGGING
				ets_uart_printf("nooLite MT1132 sent OK\r\n");
			#endif
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
	#ifdef CTRL_LOGGING
		os_printf("FindConnData: Couldn't find connection for %p\n", arg);
	#endif
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
		#ifdef CTRL_LOGGING
			os_printf("xmitSendBuff\r\n");
		#endif
		espconn_sent(conn->conn, (uint8_t*)conn->priv->sendBuff, conn->priv->sendBuffLen);
		conn->priv->sendBuffLen = 0;
	}
}

//Start the response headers.
void ICACHE_FLASH_ATTR control_httpdStartResponse(HttpdConnData *conn, int code) {
	char buff[128];
	int l;
	l = os_sprintf(buff, "HTTP/1.0 %d OK\r\nServer: nooLite-Control-Server/0.1\r\n", code);
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
