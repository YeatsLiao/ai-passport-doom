/*
 * doom_iwad.h - ESP32 override: WAD loaded from flash via mmap pointer
 *
 * This replaces GBADoom's original doom_iwad.h which declares doom_iwad
 * as an array (for compile-time embedded WAD data). On ESP32 we use a
 * pointer to mmap'd flash instead.
 *
 * Since this file is in components/doom/ (which is in INCLUDE_DIRS before
 * the GBADoom include path), it takes priority over the original.
 */
#ifndef DOOM_IWAD_H
#define DOOM_IWAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pointer to mmap'd WAD data in flash */
extern const unsigned char *doom_iwad;
extern unsigned int doom_iwad_len;

/* Init function (defined in esp32_wad.c) */
int doom_wad_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_IWAD_H */
