#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "spi_flash.h"

#include "flash_param.h"

// All data saved/loaded needs to be in multiples of 4

void ICACHE_FLASH_ATTR load_flash_param(uint32 sectorOffset, uint32 *param, uint32 size)
{
    spi_flash_read((ESP_PARAM_START_SEC + sectorOffset) * SPI_FLASH_SEC_SIZE, (uint32 *)param, size);
}

void ICACHE_FLASH_ATTR save_flash_param(uint32 sectorOffset, uint32 *param, uint32 size)
{
	spi_flash_erase_sector(ESP_PARAM_START_SEC + sectorOffset);
	spi_flash_write((ESP_PARAM_START_SEC + sectorOffset) * SPI_FLASH_SEC_SIZE, (uint32 *)param, size);
}

void ICACHE_FLASH_ATTR wipe_flash_param(uint32 sectorOffset)
{
	spi_flash_erase_sector(ESP_PARAM_START_SEC + sectorOffset);
}
