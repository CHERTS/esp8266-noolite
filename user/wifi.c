#include "osapi.h"
#include "user_interface.h"
#include "wifi.h"
#include "noolite_platform.h"

const char *WiFiMode[] =
{
		"NULL",		// 0x00
		"STATION",	// 0x01
		"SOFTAP", 	// 0x02
		"STATIONAP"	// 0x03
};

void setup_wifi_ap_mode(void)
{
	wifi_set_opmode((wifi_get_opmode()|SOFTAP_MODE)&STATIONAP_MODE);
	struct softap_config apconfig;
	if(wifi_softap_get_config(&apconfig))
	{
		wifi_softap_dhcps_stop();
		char macaddr[6];
		wifi_get_macaddr(SOFTAP_IF, macaddr);
		os_memset(apconfig.ssid, 0, sizeof(apconfig.ssid));
		os_memset(apconfig.password, 0, sizeof(apconfig.password));
		apconfig.ssid_len = os_sprintf(apconfig.ssid, "NOOLITE_%02x%02x%02x%02x%02x%02x", MAC2STR(macaddr));
		os_sprintf(apconfig.password, "%02x%02x%02x%02x%02x%02x", MAC2STR(macaddr));
		apconfig.authmode = AUTH_WPA_WPA2_PSK;
		apconfig.ssid_hidden = 0;
		apconfig.channel = 7;
		apconfig.max_connection = 10;
		if(!wifi_softap_set_config(&apconfig))
		{
			#ifdef NOOLITE_LOGGING
			ets_uart_printf("NOOLITE not set AP config!\r\n");
			#endif
		}
		struct ip_info ipinfo;
		if(wifi_get_ip_info(SOFTAP_IF, &ipinfo))
		{
			IP4_ADDR(&ipinfo.ip, 192, 168, 4, 1);
			IP4_ADDR(&ipinfo.gw, 192, 168, 4, 1);
			IP4_ADDR(&ipinfo.netmask, 255, 255, 255, 0);
			if(!wifi_set_ip_info(SOFTAP_IF, &ipinfo))
			{
				#ifdef NOOLITE_LOGGING
				ets_uart_printf("NOOLITE not set IP config!\r\n");
				#endif
			} else {
				#ifdef NOOLITE_LOGGING
				char temp[80];
				os_sprintf(temp, "CONFIGURATION WEB SERVER IP: " IPSTR "\r\n", IP2STR(&ipinfo.ip));
				ets_uart_printf(temp);
				#endif
			}
		}
		wifi_softap_dhcps_start();
	}
	if(wifi_get_phy_mode() != PHY_MODE_11N)
		wifi_set_phy_mode(PHY_MODE_11N);
	if(wifi_station_get_auto_connect() == 0)
		wifi_station_set_auto_connect(1);
	#ifdef NOOLITE_LOGGING
	char temp[100];
	ets_uart_printf("NOOLITE in AP mode configured.\r\n");
	if(wifi_softap_get_config(&apconfig)) {
		os_sprintf(temp, "AP config: SSID: %s, PASSWORD: %s, CHANNEL: %u\r\n", apconfig.ssid,	apconfig.password, apconfig.channel);
		ets_uart_printf(temp);
	}
	#endif
}

void setup_wifi_st_mode(struct station_config stationConf)
{
	//wifi_set_opmode((wifi_get_opmode()|STATION_MODE)&STATIONAP_MODE);
	wifi_station_disconnect();
	wifi_station_dhcpc_stop();
	if(!wifi_station_set_config(&stationConf))
	{
		#ifdef NOOLITE_LOGGING
		ets_uart_printf("NOOLITE not set station config!\r\n");
		#endif
	}
	wifi_station_connect();
	wifi_station_dhcpc_start();
	wifi_station_set_auto_connect(1);
	if(wifi_get_phy_mode() != PHY_MODE_11N)
		wifi_set_phy_mode(PHY_MODE_11N);
	if(wifi_station_get_auto_connect() == 0)
		wifi_station_set_auto_connect(1);
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("NOOLITE in STA mode configured.\r\n");
	#endif
}
