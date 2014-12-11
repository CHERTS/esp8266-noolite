#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "os_type.h"
#include "driver/uart.h"
#include "gpio.h"

#include "flash_param.h"
#include "noolite_platform.h"
#include "noolite_config_server.h"
#include "noolite_control_server.h"

//os_timer_t ConfigChecker;
//static int resetCnt = 0;
os_timer_t WiFiLinker;
os_timer_t DebounceTimer;
static tConnState connState = WIFI_CONNECTING;
uint16_t wifiErrorConnect = 0;
uint16_t controlServerStatus = 0;

LOCAL void input_intr_handler(void *arg);
void ICACHE_FLASH_ATTR debounce_timer_cb(void *arg);

static void ICACHE_FLASH_ATTR noolite_platform_check_ip(void *arg)
{
    struct ip_info ipconfig;

    os_timer_disarm(&WiFiLinker);

    wifi_get_ip_info(STATION_IF, &ipconfig);

    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0)
    {
        connState = WIFI_CONNECTED;
        #ifdef NOOLITE_LOGGING
        	char temp[80];
        	ets_uart_printf("WiFi connected\r\n");
        	os_sprintf(temp, "Client IP address: " IPSTR "\r\n", IP2STR(&ipconfig.ip));
			ets_uart_printf(temp);
			os_sprintf(temp, "Client IP netmask: " IPSTR "\r\n", IP2STR(&ipconfig.netmask));
			ets_uart_printf(temp);
			os_sprintf(temp, "Client IP gateway: " IPSTR "\r\n", IP2STR(&ipconfig.gw));
			ets_uart_printf(temp);
        #endif
        wifiErrorConnect = 0;
        if(controlServerStatus == 0) {
        	controlServerStatus++;
			#ifdef NOOLITE_LOGGING
        	ets_uart_printf("Init noolite control server...\r\n");
			#endif
        	noolite_control_server_init();
        }
        //os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
        //os_timer_arm(&WiFiLinker, 10000, 0);
    }
    else
    {
		if(wifi_station_get_connect_status() == STATION_WRONG_PASSWORD)
		{
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting error, wrong password\r\n");
			#endif
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1500, 0);
		}
		else if(wifi_station_get_connect_status() == STATION_NO_AP_FOUND)
		{
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting error, ap not found\r\n");
			#endif
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
		}
		else if(wifi_station_get_connect_status() == STATION_CONNECT_FAIL)
		{
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting fail\r\n");
			#endif
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
		}
		else
		{
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
			connState = WIFI_CONNECTING;
			wifiErrorConnect++;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting...\r\n");
			#endif
		}
		/*if (wifiErrorConnect > 10)
		{
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting failed, system restart...\r\n");
			#endif
			system_restart();
		}*/
		controlServerStatus = 0;
    }
}

static void ICACHE_FLASH_ATTR noolite_platform_enter_configuration_mode(void)
{
	wipe_flash_param(ESP_PARAM_SAVE_1);
	wifi_set_opmode(STATIONAP_MODE);
	#ifdef NOOLITE_LOGGING
	ets_uart_printf("Restarting in STATIONAP mode...\r\n");
	#endif
	system_restart();
}

/*static void ICACHE_FLASH_ATTR noolite_platform_config_checker(void *arg)
{
	if (!GPIO_INPUT_GET(BTN_CONFIG_GPIO)) {
		resetCnt++;
	}
	else {
		if (resetCnt >= 6) { //3 sec pressed
			noolite_platform_enter_configuration_mode();
		}
		resetCnt = 0;
	}
}

void BtnInit() {
	// Select pin function
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
	// Disable pulldown
	PIN_PULLDWN_DIS(PERIPHS_IO_MUX_GPIO0_U);
	// Enable pull up R
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);
	// Set GPIO0 as input mode
	gpio_output_set(0, 0, 0, BIT0);
	os_timer_disarm(&ConfigChecker);
	os_timer_setfn(&ConfigChecker, noolite_platform_config_checker, NULL);
	os_timer_arm(&ConfigChecker, 500, 1);
}*/

void BtnInit() {
	// Set GPIO 0 to IO
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO0);
	// Enable pull up R
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTDI_U);
	// Disable interrupts
	ETS_GPIO_INTR_DISABLE();
	// Attach pin 0 to the interrupt thing
	ETS_GPIO_INTR_ATTACH(input_intr_handler, NULL);
	GPIO_DIS_OUTPUT(BTN_CONFIG_GPIO);
	// Clear gpio status
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(0));
	// Enable interrupt
	gpio_pin_intr_state_set(GPIO_ID_PIN(BTN_CONFIG_GPIO), GPIO_PIN_INTR_NEGEDGE);
	// Global re-enable interrupts
	ETS_GPIO_INTR_ENABLE();
	os_timer_disarm(&DebounceTimer);
	os_timer_setfn(&DebounceTimer, &debounce_timer_cb, 0);
}

LOCAL void input_intr_handler(void *arg)
{
  // Not that sure what this does yet and where the register is used for
  uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
  // Ñlear interrupt status
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
  // Disable interrupt
  ETS_GPIO_INTR_DISABLE();
  gpio_pin_intr_state_set(GPIO_ID_PIN(BTN_CONFIG_GPIO), GPIO_PIN_INTR_DISABLE);
  // Enable interrupt
  ETS_GPIO_INTR_ENABLE();
  os_timer_arm(&DebounceTimer, 200, FALSE);
}

void ICACHE_FLASH_ATTR debounce_timer_cb(void *arg)
{
	ETS_GPIO_INTR_DISABLE();
	gpio_pin_intr_state_set(GPIO_ID_PIN(BTN_CONFIG_GPIO), GPIO_PIN_INTR_NEGEDGE);
	ETS_GPIO_INTR_ENABLE();
	#ifdef NOOLITE_LOGGING
		ets_uart_printf("Button CONFMODE pressed, wiping configuration and restart in configuration mode...\r\n");
	#endif
	noolite_platform_enter_configuration_mode();
}

void ICACHE_FLASH_ATTR noolite_platform_init(void)
{
	tSetup nooLiteSetup;

	load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooLiteSetup, sizeof(tSetup));

	BtnInit();

	if(nooLiteSetup.SetupOk != SETUP_OK_KEY || wifi_get_opmode() != STATION_MODE)
	{
		//wipe_flash_param(ESP_PARAM_SAVE_1);

		if(wifi_get_opmode() != STATIONAP_MODE)
		{
			wifi_set_opmode(STATIONAP_MODE);
			#ifdef NOOLITE_LOGGING
			ets_uart_printf("Restarting in STATIONAP mode...\r\n");
			#endif
			system_restart();
		}

		char macaddr[6];
		wifi_get_macaddr(SOFTAP_IF, macaddr);

		struct ip_info ipinfo;
		IP4_ADDR(&ipinfo.ip, 10, 10, 10, 1);
		IP4_ADDR(&ipinfo.gw, 10, 10, 10, 1);
		IP4_ADDR(&ipinfo.netmask, 255, 255, 255, 0);
		wifi_set_ip_info(SOFTAP_IF, &ipinfo);

		struct softap_config apConfig;
		os_memset(apConfig.ssid, 0, sizeof(apConfig.ssid));
		os_sprintf(apConfig.ssid, "NOOLITE_%02x%02x%02x%02x%02x%02x", MAC2STR(macaddr));
		os_memset(apConfig.password, 0, sizeof(apConfig.password));
		os_sprintf(apConfig.password, "%02x%02x%02x%02x%02x%02x", MAC2STR(macaddr));
		apConfig.authmode = AUTH_WPA_WPA2_PSK;
		apConfig.channel = 7;
		apConfig.max_connection = 255;
		apConfig.ssid_hidden = 0;

		wifi_softap_set_config(&apConfig);

		#ifdef NOOLITE_LOGGING
		char temp[80];
		os_sprintf(temp, "OPMODE: %u\r\n", wifi_get_opmode());
		ets_uart_printf(temp);
		os_sprintf(temp, "SSID: %s\r\n", apConfig.ssid);
		ets_uart_printf(temp);
		os_sprintf(temp, "PASSWORD: %s\r\n", apConfig.password);
		ets_uart_printf(temp);
		os_sprintf(temp, "CHANNEL: %u\r\n", apConfig.channel);
		ets_uart_printf(temp);
		os_sprintf(temp, "CONFIGURATION WEB SERVER IP: " IPSTR "\r\n", IP2STR(&ipinfo.ip));
		ets_uart_printf(temp);
		ets_uart_printf("Starting nooLite configuration web server...\r\n");
		#endif

		noolite_config_server_init();
		//noolite_control_server_init();
	}
	else
	{
		#ifdef NOOLITE_LOGGING
			ets_uart_printf("Starting in normal mode...\r\n");
		#endif

		if(wifi_get_opmode() != STATION_MODE)
		{
			#ifdef NOOLITE_LOGGING
			ets_uart_printf("Restarting in STATION mode...\r\n");
			#endif
			wifi_set_opmode(STATION_MODE);
			system_restart();
		}

		#ifdef NOOLITE_LOGGING
		char temp[80];
		uint8 bssid[6];
		struct station_config stationConf;
		wifi_station_get_config(&stationConf);
		os_sprintf(temp, "OPMODE: %u\r\n", wifi_get_opmode());
		ets_uart_printf(temp);
		os_sprintf(temp, "AP SSID: %s\r\n", stationConf.ssid);
		ets_uart_printf(temp);
		os_sprintf(temp, "AP PASSWORD: %s\r\n", stationConf.password);
		ets_uart_printf(temp);
		wifi_get_macaddr(STATION_IF, bssid);
		os_sprintf(temp, "STA MACADDR: " MACSTR "\r\n", MAC2STR(bssid));
		ets_uart_printf(temp);
		#endif

		#ifdef NOOLITE_LOGGING
		ets_uart_printf("System initialization done!\r\n");
		#endif

		os_timer_disarm(&WiFiLinker);
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 100, 0);

	}
}
