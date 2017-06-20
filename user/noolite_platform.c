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
#include "wifi.h"

//os_timer_t ConfigChecker;
//static int resetCnt = 0;
os_timer_t WiFiLinker;
os_timer_t DebounceTimer;
uint16_t wifiErrorConnect = 0;
uint16_t controlServerStatus = 0;

LOCAL void input_intr_handler(void *arg);
void ICACHE_FLASH_ATTR debounce_timer_cb(void *arg);

static void ICACHE_FLASH_ATTR noolite_platform_check_ip(void *arg)
{
    struct ip_info ipconfig;

    os_timer_disarm(&WiFiLinker);

    wifi_get_ip_info(STATION_IF, &ipconfig);

    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {
       	ESPOOLITE_LOGGING("WiFi connected\r\n");
       	ESPOOLITE_LOGGING("Client IP address: " IPSTR "\r\n", IP2STR(&ipconfig.ip));
	ESPOOLITE_LOGGING("Client IP netmask: " IPSTR "\r\n", IP2STR(&ipconfig.netmask));
	ESPOOLITE_LOGGING("Client IP gateway: " IPSTR "\r\n", IP2STR(&ipconfig.gw));
        wifiErrorConnect = 0;
        if(controlServerStatus == 0) {
        	controlServerStatus++;
        	ESPOOLITE_LOGGING("Init noolite control server...\r\n");
        	noolite_control_server_init();
        }
        //os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
        //os_timer_arm(&WiFiLinker, 10000, 0);
    } else {
		if(wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) {
			ESPOOLITE_LOGGING("WiFi connecting error, wrong password\r\n");
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1500, 0);
		} else if(wifi_station_get_connect_status() == STATION_NO_AP_FOUND) {
			ESPOOLITE_LOGGING("WiFi connecting error, ap not found\r\n");
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
		} else if(wifi_station_get_connect_status() == STATION_CONNECT_FAIL) {
			ESPOOLITE_LOGGING("WiFi connecting fail\r\n");
			wifiErrorConnect++;
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
		} else {
			os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
			os_timer_arm(&WiFiLinker, 1000, 0);
			wifiErrorConnect++;
			ESPOOLITE_LOGGING("WiFi connecting...\r\n");
		}
		/*if (wifiErrorConnect > 10)
		{
			ESPOOLITE_LOGGING("WiFi connecting failed, system restart...\r\n");
			system_restart();
		}*/
		controlServerStatus = 0;
    }
}

static void ICACHE_FLASH_ATTR noolite_platform_enter_configuration_mode(void)
{
	wipe_flash_param(ESP_PARAM_SAVE_1);
	wifi_set_opmode(STATIONAP_MODE);
	ESPOOLITE_LOGGING("Restarting in STATIONAP mode...\r\n");
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
	ESPOOLITE_LOGGING("Button CONFMODE pressed, wiping configuration and restart in configuration mode...\r\n");
	noolite_platform_enter_configuration_mode();
}

void ICACHE_FLASH_ATTR noolite_platform_init(void)
{
	tSetup nooLiteSetup;
	struct station_config stationConf;

	load_flash_param(ESP_PARAM_SAVE_1, (uint32 *)&nooLiteSetup, sizeof(tSetup));

	BtnInit();

	if(nooLiteSetup.SetupOk != SETUP_OK_KEY || wifi_get_opmode() != STATION_MODE) {
		// Init WiFi in STATIONAP mode
		setup_wifi_ap_mode();
		// Start config server
		noolite_config_server_init();
	} else {
		ESPOOLITE_LOGGING("Starting in normal mode...\r\n");

		if(wifi_get_opmode() != STATION_MODE) {
			ESPOOLITE_LOGGING("Start in STATION mode...\r\n");
			wifi_set_opmode(STATION_MODE);
		}
		wifi_station_set_auto_connect(1);
		if(wifi_get_phy_mode() != PHY_MODE_11N)
			wifi_set_phy_mode(PHY_MODE_11N);
		if(wifi_station_get_auto_connect() == 0)
			wifi_station_set_auto_connect(1);

		wifi_station_get_config(&stationConf);
		ESPOOLITE_LOGGING("OPMODE: %u, SSID: %s, PWD: %s\r\n", wifi_get_opmode(), stationConf.ssid, stationConf.password);
		ESPOOLITE_LOGGING("System initialization done!\r\n");

		os_timer_disarm(&WiFiLinker);
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)noolite_platform_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 100, 0);

	}
}
