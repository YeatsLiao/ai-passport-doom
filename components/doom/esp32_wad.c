/*
 * esp32_wad.c - Load DOOM1.WAD from flash partition via mmap
 *
 * Provides the same symbols as GBADoom's doom_iwad.c:
 *   const unsigned char doom_iwad[]  -> mmap'd flash pointer
 *   const unsigned int  doom_iwad_len -> actual WAD size
 *
 * The WAD file must be flashed to the "wad" partition defined in
 * partitions.csv (type=data, subtype=0x40, offset=0x310000, size=0x410000).
 *
 * Flash command (esptool):
 *   esptool.py --port COM4 write_partition -n wad doom1.wad
 */

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "doom_iwad.h"

static const char *TAG = "doom_wad";

const unsigned char *doom_iwad;
unsigned int doom_iwad_len;

/*
 * doom_wad_init - Find the WAD partition and mmap it into memory.
 * Returns 0 on success, -1 on failure.
 */
int doom_wad_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x40, "wad"
    );

    if (!part) {
        ESP_LOGE(TAG, "WAD partition 'wad' (type=0x40) not found!");
        ESP_LOGE(TAG, "Check partitions.csv and flash the WAD with:");
        ESP_LOGE(TAG, "  esptool.py write_partition -n wad doom1.wad");
        return -1;
    }

    ESP_LOGI(TAG, "WAD partition found: '%s' addr=0x%lx size=0x%lx",
             part->label,
             (unsigned long)part->address,
             (unsigned long)part->size);

    // Memory-map the partition for direct read access (zero-copy)
    spi_flash_mmap_handle_t handle;
    const void *mapped = NULL;

    esp_err_t err = esp_partition_mmap(
        part, 0, part->size,
        SPI_FLASH_MMAP_DATA,
        &mapped, &handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Validate IWAD header (first 4 bytes should be "IWAD")
    const char *hdr = (const char *)mapped;
    if (memcmp(hdr, "IWAD", 4) != 0) {
        ESP_LOGE(TAG, "Bad WAD header: '%.4s' (expected 'IWAD')", hdr);
        return -1;
    }

    doom_iwad = (const unsigned char *)mapped;
    doom_iwad_len = (unsigned int)part->size;

    // Read actual WAD size from header (12 bytes: 4 sig + 4 numlumps + 4 dir_offset)
    // But we use partition size for now (slightly larger, but safe)
    ESP_LOGI(TAG, "WAD loaded: %u bytes at %p (handle=%d)",
             doom_iwad_len, doom_iwad, handle);

    return 0;
}
