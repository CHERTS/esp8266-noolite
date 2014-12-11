#ifndef __FLASH_PARAM_H
#define __FLASH_PARAM_H

#include "c_types.h"

// 25Q40 = 512  KB (4 Mbit)
// 25Q80 = 1024 KB (8 Mbit)

/* NOTICE
 * this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash.
 */
#define ESP_PARAM_START_SEC		0x3C

#define ESP_PARAM_SAVE_0	0
#define ESP_PARAM_SAVE_1	1
#define ESP_PARAM_SAVE_2	2
#define ESP_PARAM_SAVE_3	3

// public
void ICACHE_FLASH_ATTR load_flash_param(uint32, uint32 *, uint32);
void ICACHE_FLASH_ATTR save_flash_param(uint32, uint32 *, uint32);
void ICACHE_FLASH_ATTR wipe_flash_param(uint32 sectorOffset);

#endif
