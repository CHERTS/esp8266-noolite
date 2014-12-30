#include "ets_sys.h"
#include "osapi.h"
#include "noolite_platform.h"
#include "driver/uart.h"

extern int ets_uart_printf(const char *fmt, ...);

void user_init(void)
{
	uart_init(BIT_RATE_9600, BIT_RATE_9600);
	os_delay_us(1000);

	#ifdef NOOLITE_LOGGING
	ets_uart_printf("\r\nnooLite platform starting...\r\n");
	#endif

	noolite_platform_init();

	#ifdef NOOLITE_LOGGING
	ets_uart_printf("nooLite platform started!\r\n");
	#endif
}
