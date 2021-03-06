#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

#include "noolite_config_server.h"
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
static HttpdConnData connData[MAX_CONN];

ETSTimer returnToNormalModeTimer;
static struct espconn esp_conn;
static esp_tcp esptcp;
static unsigned char killConn;
static unsigned char returnToNormalMode;
// html page header and footer
static const char *pageStart = "<html><head><title>ESPOOLITE Base Config</title><style>body{font-family: Arial}</style></head><body><form method=\"get\" action=\"/\"><input type=\"hidden\" name=\"save\" value=\"1\">\r\n";
#ifdef NO_COPYRIGHT
static const char *pageEnd = "</form><hr>ESPOOLITE v{version} (c) 2014-2016</body></html>\r\n";
#endif
#ifndef NO_COPYRIGHT
static const char *pageEnd = "</form><hr>ESPOOLITE v{version} (c) 2014-2016 by <a href=\"mailto:sleuthhound@gmail.com\" target=\"_blank\">Mikhail Grigorev</a>, <a href=\"http://programs74.ru\" target=\"_blank\">programs74.ru</a></body></html>\r\n";
#endif
// html pages
static const char *pageIndex = "<h2>ESPOOLITE Base Config</h2><ul><li><a href=\"?page=wifi\">WiFi settings</a></li><li><a href=\"?page=noolite\">ESPOOLITE settings</a></li><li><a href=\"?page=return\">Return to normal mode</a></li></ul>\r\n";
static const char *pageSetWifi = "<h2><a href=\"/\">Home</a> / WiFi settings</h2><input type=\"hidden\" name=\"page\" value=\"wifi\"><table border=\"0\"><tr><td><b>AP SSID:</b></td><td><input type=\"text\" name=\"ssid\" value=\"{ssid}\" size=\"32\"></td></tr><tr><td><b>AP Password:</b></td><td><input type=\"password\" name=\"pass\" value=\"{pass}\" size=\"32\"></td></tr><tr><td><b>Status:</b></td><td>{status} <a href=\"?page=wifi\">[refresh]</a></td></tr><tr><td></td><td><input type=\"submit\" value=\"Save\"></td></tr></table>\r\n";
static const char *pageSetNoolite = "<h2><a href=\"/\">Home</a> / ESPOOLITE settings</h2><input type=\"hidden\" name=\"page\" value=\"noolite\"><table border=\"0\"><tr><td><b>Device ID:</b></td><td><input type=\"text\" name=\"deviceid\" value=\"{deviceid}\" size=\"40\" maxlength=\"32\">&nbsp;32 characters</td></tr><tr><td></td><td><input type=\"submit\" value=\"Save\"></td><td></td></tr></table>\r\n";
static const char *pageResetStarted = "<h1>Returning to normal mode...</h1>You can close this window now.\r\n";
static const char *pageSavedInfo = "<br><b style=\"color: green\">Settings saved!</b>\r\n";

static void ICACHE_FLASH_ATTR return_to_normal_mode_cb(void *arg)
{
	wifi_station_disconnect();
	wifi_set_opmode(STATION_MODE);
	ESPOOLITE_LOGGING("Restarting in STATION mode...\r\n");
	system_restart();
}

static void ICACHE_FLASH_ATTR noolite_config_server_recon(void *arg, sint8 err)
{
	ESPOOLITE_LOGGING("noolite_config_server_recon\r\n");
}

static void ICACHE_FLASH_ATTR noolite_config_server_discon(void *arg)
{
	ESPOOLITE_LOGGING("noolite_config_server_discon\r\n");
	//Just look at all the sockets and kill the slot if needed.
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connData[i].conn!=NULL) {
			//Why the >=ESPCONN_CLOSE and not ==? Well, seems the stack sometimes de-allocates
			//espconns under our noses, especially when connections are interrupted. The memory
			//is then used for something else, and we can use that to capture *most* of the
			//disconnect cases.
			if (connData[i].conn->state==ESPCONN_NONE || connData[i].conn->state>=ESPCONN_CLOSE) {
				connData[i].conn=NULL;
				config_httpdRetireConn(&connData[i]);
			}
		}
	}
}

static void ICACHE_FLASH_ATTR noolite_config_server_recv(void *arg, char *data, unsigned short len)
{
	ESPOOLITE_LOGGING("noolite_config_server_recv\r\n");

	char sendBuff[MAX_SENDBUFF_LEN];
	HttpdConnData *conn = config_httpdFindConnData(arg);

	if (conn==NULL)
		return;
	conn->priv->sendBuff = sendBuff;
	conn->priv->sendBuffLen = 0;

	if(os_strncmp(data, "GET ", 4) == 0)
	{
		char page[16];
		os_memset(page, 0, sizeof(page));
		noolite_config_server_get_key_val("page", sizeof(page), data, page);
		noolite_config_server_process_page(conn, page, data);
	}
	else
	{
		const char *notfound="404 Not Found (or method not implemented).";
		config_httpdStartResponse(conn, 404);
		config_httpdHeader(conn, "Content-Type", "text/plain");
		config_httpdEndHeaders(conn);
		config_httpdSend(conn, notfound, -1);
		killConn = 1;
	}
	config_xmitSendBuff(conn);
	return;
}

static void ICACHE_FLASH_ATTR noolite_config_server_process_page(struct HttpdConnData *conn, char *page, char *request)
{
	char ssid[32];
	char pass[64];
	char buff[1024];
	char html_buff[1024];
	char version_buff[10];
	int len;
	static struct ip_info ipConfig;
	char save[2] = {'0', '\0'};
	char status[32] = "[status]";
	struct station_config stationConf;

	os_memset(status, 0, sizeof(status));
	os_memset(ssid, 0, sizeof(ssid));
	os_memset(pass, 0, sizeof(pass));

	config_httpdStartResponse(conn, 200);
	config_httpdHeader(conn, "Content-Type", "text/html");
	config_httpdEndHeaders(conn);

	// page header
	len = os_sprintf(buff, pageStart);
	if(!config_httpdSend(conn, buff, len)) {
		ESPOOLITE_LOGGING("Error httpdSend: pageStart out-of-memory\r\n");
	}

	noolite_config_server_get_key_val("save", sizeof(save), request, save);

	// wifi settings page
	if(os_strncmp(page, "wifi", 4) == 0 && strlen(page) == 4)
	{
		if(wifi_station_get_config(&stationConf)) {
			ESPOOLITE_LOGGING("STA config: SSID: %s, PASSWORD: %s\r\n", stationConf.ssid, stationConf.password);
		}

		if(save[0] == '1')
		{
			os_memset(stationConf.ssid, 0, sizeof(stationConf.ssid));
			os_memset(stationConf.password, 0, sizeof(stationConf.password));
			noolite_config_server_get_key_val("ssid", sizeof(ssid), request, ssid); //32
			if(strlen(ssid) != 0)
				os_sprintf(stationConf.ssid, "%s", ssid);
			else
				os_sprintf(stationConf.ssid, "%s", "Test");
			noolite_config_server_get_key_val("pass", sizeof(pass), request, pass); //64
			if(strlen(pass) != 0)
				os_sprintf(stationConf.password, "%s", pass);
			else
				os_sprintf(stationConf.password, "%s", "test");
			// Init WiFi in STA mode
			setup_wifi_st_mode(stationConf);
			if(wifi_station_get_config(&stationConf)) {
				ESPOOLITE_LOGGING("New STA config: SSID: %s, PASSWORD: %s\r\n", stationConf.ssid, stationConf.password);
			}
		}

		if(strlen(stationConf.ssid) == 0) {
			os_sprintf(ssid, "Test");
		} else {
			os_sprintf(ssid, "%s", stationConf.ssid);
		}
		if(strlen(stationConf.password) == 0) {
			os_sprintf(pass, "test");
		} else {
			os_sprintf(pass, "%s", stationConf.password);
		}
		os_sprintf(html_buff, "%s", str_replace(pageSetWifi, "{ssid}", ssid));
		os_sprintf(html_buff, "%s", str_replace(html_buff, "{pass}", pass));

		if(wifi_get_opmode() == STATION_MODE || wifi_get_opmode() == STATIONAP_MODE)
		{
			switch(wifi_station_get_connect_status())
			{
				case STATION_GOT_IP:
					wifi_get_ip_info(STATION_IF, &ipConfig);
					if(ipConfig.ip.addr != 0) {
						os_sprintf(status, "Connected");
					} else {
						os_sprintf(status, "Connected, ipaddr is null");
					}
					break;
				case STATION_WRONG_PASSWORD:
					os_sprintf(status, "Wrong Password");
					break;
				case STATION_NO_AP_FOUND:
					os_sprintf(status, "AP Not Found");
					break;
				case STATION_CONNECT_FAIL:
					os_sprintf(status, "Connect Failed");
					break;
				default:
					os_sprintf(status, "Not Connected");
			}
		}

		os_sprintf(html_buff, "%s", str_replace(html_buff, "{status}", status));

		if(save[0] == '1')
		{
			char buff_saved[512];
			os_sprintf(buff_saved, "%s%s", html_buff, pageSavedInfo);
			len = os_sprintf(buff, buff_saved);
			config_httpdSend(conn, buff, len);
		} else {
			len = os_sprintf(buff, html_buff);
			config_httpdSend(conn, buff, len);
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

		char i;
		char *result;
		bin2strhex((char *)nooLiteSetup.deviceId, sizeof(nooLiteSetup.deviceId), &result);
		os_sprintf(html_buff, "%s", str_replace(pageSetNoolite, "{deviceid}", result));
		os_free(result);

		if( save[0] == '1' )
		{
			char buff_saved[1024];
			os_sprintf(buff_saved, "%s%s", html_buff, pageSavedInfo);
			len = os_sprintf(buff, buff_saved);
			config_httpdSend(conn, buff, len);
		} else {
			len = os_sprintf(buff, html_buff);
			config_httpdSend(conn, buff, len);
		}
	}
	else if(os_strncmp(page, "return", 3) == 0 && strlen(page) == 6)
	{
		len = os_sprintf(buff, pageResetStarted);
		config_httpdSend(conn, buff, len);
		returnToNormalMode = 1;
	}
	else
	{
		len = os_sprintf(buff, pageIndex);
		if(!config_httpdSend(conn, buff, len)){
			ESPOOLITE_LOGGING("Error httpdSend: pageIndex out-of-memory\r\n");
		}
	}
	// page footer
	//len = os_sprintf(buff, pageEnd);
	os_sprintf(version_buff, "%s", ESPOOLITE_VERSION);
	len = os_sprintf(buff, "%s", str_replace(pageEnd, "{version}", version_buff));
	if(!config_httpdSend(conn, buff, len)){
		ESPOOLITE_LOGGING("Error httpdSend: pageEnd out-of-memory\r\n");
	}
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
	HttpdConnData *conn = config_httpdFindConnData(arg);

	if (conn==NULL) return;

	if(killConn)
	{
		espconn_disconnect(conn->conn);
		if(returnToNormalMode)
		{
			//wifi_station_disconnect();
			//wifi_set_opmode(STATION_MODE);
			os_timer_arm(&returnToNormalModeTimer, 500, 0);
		}
	}
}

static void ICACHE_FLASH_ATTR noolite_config_server_connect(void *arg)
{
	ESPOOLITE_LOGGING("noolite_config_server_connect\r\n");

	struct espconn *conn=arg;
	int i;
	//Find empty conndata in pool
	for (i=0; i<MAX_CONN; i++)
		if (connData[i].conn==NULL) break;
	ESPOOLITE_LOGGING("Con req, conn=%p, pool slot %d\n", conn, i);
	if (i==MAX_CONN) {
		ESPOOLITE_LOGGING("Conn pool overflow!\r\n");
		espconn_disconnect(conn);
		return;
	}
	connData[i].conn = conn;
	connData[i].postLen = 0;
	connData[i].priv = &connPrivData[i];

	espconn_regist_recvcb(conn, noolite_config_server_recv);
	espconn_regist_sentcb(conn, noolite_config_server_sent);
	espconn_regist_reconcb(conn, noolite_config_server_recon);
	espconn_regist_disconcb(conn, noolite_config_server_discon);
}

void ICACHE_FLASH_ATTR noolite_config_server_init()
{
	int i;
	ESPOOLITE_LOGGING("noolite_config_server_init()\r\n");

	os_timer_disarm(&returnToNormalModeTimer);
	os_timer_setfn(&returnToNormalModeTimer, return_to_normal_mode_cb, 0);

	for (i=0; i<MAX_CONN; i++) {
		connData[i].conn=NULL;
	}

	esptcp.local_port = 80;
	esp_conn.type = ESPCONN_TCP;
	esp_conn.state = ESPCONN_NONE;
	esp_conn.proto.tcp = &esptcp;
	espconn_regist_connectcb(&esp_conn, noolite_config_server_connect);
	espconn_accept(&esp_conn);
}

//Looks up the connData info for a specific esp connection
static ICACHE_FLASH_ATTR HttpdConnData ICACHE_FLASH_ATTR *config_httpdFindConnData(void *arg) {
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connData[i].conn==(struct espconn *)arg)
			return &connData[i];
	}
	ESPOOLITE_LOGGING("FindConnData: Couldn't find connection for %p\n", arg);
	return NULL; //WtF?
}

//Add data to the send buffer. len is the length of the data. If len is -1
//the data is seen as a C-string.
//Returns 1 for success, 0 for out-of-memory.
int ICACHE_FLASH_ATTR config_httpdSend(HttpdConnData *conn, const char *data, int len) {
	if (len<0)
		len = strlen(data);
	if (conn->priv->sendBuffLen+len > MAX_SENDBUFF_LEN)
		return 0;
	os_memcpy(conn->priv->sendBuff+conn->priv->sendBuffLen, data, len);
	conn->priv->sendBuffLen += len;
	return 1;
}

//Helper function to send any data in conn->priv->sendBuff
static ICACHE_FLASH_ATTR void ICACHE_FLASH_ATTR config_xmitSendBuff(HttpdConnData *conn) {
	if (conn->priv->sendBuffLen != 0) {
		ESPOOLITE_LOGGING("xmitSendBuff\r\n");
		espconn_sent(conn->conn, (uint8_t*)conn->priv->sendBuff, conn->priv->sendBuffLen);
		conn->priv->sendBuffLen = 0;
	}
}

//Start the response headers.
void ICACHE_FLASH_ATTR config_httpdStartResponse(HttpdConnData *conn, int code) {
	char buff[128];
	int l;
	l = os_sprintf(buff, "HTTP/1.0 %d OK\r\nServer: ESPOOLITE-Config-Server/0.1\r\n", code);
	config_httpdSend(conn, buff, l);
}

//Send a http header.
void ICACHE_FLASH_ATTR config_httpdHeader(HttpdConnData *conn, const char *field, const char *val) {
	char buff[256];
	int l;
	l = os_sprintf(buff, "%s: %s\r\n", field, val);
	config_httpdSend(conn, buff, l);
}

//Finish the headers.
void ICACHE_FLASH_ATTR config_httpdEndHeaders(HttpdConnData *conn) {
	config_httpdSend(conn, "\r\n", -1);
}

static void ICACHE_FLASH_ATTR config_httpdRetireConn(HttpdConnData *conn) {
	conn->conn=NULL;
}
