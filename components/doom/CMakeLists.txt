# Doom engine component - wraps GBADoom sources for ESP32
#
# GBADoom source tree is compiled directly, excluding platform-specific files
# that are replaced by our ESP32 implementations.

set(GBADOOM_SRC "${GBADOOM_PATH}/source")

# Collect all .c files from GBADoom/source/
file(GLOB DOOM_ALL_C "${GBADOOM_SRC}/*.c")

# Exclude files we replace or don't need:
#   i_main.c           - has main(), we use app_main()
#   i_system_gba.cpp   - GBA-only (#ifdef GBA)
#   i_system_e32.cpp   - Windows/Qt implementation
#   i_audio.c          - GBA maxmod audio (will revisit for ESP32 audio)
#   doom_iwad.c        - compile-time WAD embedding (we load from flash)
#   fixeddiv.s         - ARM assembly (non-GBA path uses C)
list(FILTER DOOM_ALL_C EXCLUDE REGEX "i_main\\.c$")
list(FILTER DOOM_ALL_C EXCLUDE REGEX "i_system_gba\\.cpp$")
list(FILTER DOOM_ALL_C EXCLUDE REGEX "i_system_e32\\.cpp$")
# i_audio.c is kept - all GBA audio code is behind #ifdef GBA, non-GBA stubs work fine
list(FILTER DOOM_ALL_C EXCLUDE REGEX "doom_iwad\\.c$")

# ESP32 platform layer files (in this directory)
set(ESP32_SRCS
    "i_system_esp32.c"
    "esp32_wad.c"
)

idf_component_register(
    SRCS ${DOOM_ALL_C} ${ESP32_SRCS}
    INCLUDE_DIRS
        "."  # before GBADoom/include so our doom_iwad.h override wins
        "${GBADOOM_PATH}/include"
    REQUIRES
        bsp_doom
        esp_partition
        spi_flash
    PRIV_REQUIRES
        esp_timer
)

# Compile flags: suppress noisy warnings from legacy PrBoom code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-format
    -Wno-implicit-fallthrough
    -Wno-maybe-uninitialized
    -Wno-stringop-truncation
    -Wno-error=unused-value
    -Wno-error=switch
    -Wno-error=enum-compare
    -Wno-error=pointer-sign
    -Wno-error=implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=uninitialized
    -Wno-error=unused-but-set-parameter
    -Wno-error=misleading-indentation
    -Wno-error=return-type
    -Wno-error=address
    -Wno-error=array-bounds
    -Wno-error=stringop-overflow
    -Wno-error=stringop-overread
    -Wno-error=parentheses
    -Wno-error=comment
)

# Redirect newlib clock() to __wrap_clock in i_system_esp32.c.
# GBADoom's I_GetTime uses clock(); newlib's version returns 0 on ESP32,
# which deadlocks the engine's timing loops.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=clock")
