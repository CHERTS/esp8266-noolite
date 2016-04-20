#include "ets_sys.h"
#include "osapi.h"
#include "noolite_platform.h"
#include "driver/uart.h"

extern int ets_uart_printf(const char *fmt, ...);
int (*console_printf)(const char *fmt, ...) = ets_uart_printf;

void user_rf_pre_init(void)
{
}

void user_init(void)
{
	uart_init(BIT_RATE_9600, BIT_RATE_9600);
	os_delay_us(1000);
	ESPOOLITE_LOGGING("\r\nnooLite platform starting...\r\n");
	noolite_platform_init();
	ESPOOLITE_LOGGING("nooLite platform started!\r\n");
}
