file(GLOB_RECURSE APPS_SRCS
    apps/*.c
    apps/*.cpp
)

file(GLOB_RECURSE HAL_SRCS
    hal/*.c
    hal/*.cpp
)

file(GLOB_RECURSE SETTINGS_SRCS
    settings/*.c
    settings/*.cpp
)

file(GLOB_RECURSE PROTOS_SRCS
    meshtastic/*.c
    meshtastic/*.cpp
)

file(GLOB_RECURSE MESH_SRCS
    mesh/*.c
    mesh/*.cpp
)

idf_component_register(SRCS "main.cpp" ${APPS_SRCS} ${HAL_SRCS} ${SETTINGS_SRCS} ${PROTOS_SRCS} ${MESH_SRCS}
                    INCLUDE_DIRS "." "hal"
                    REQUIRES LovyanGFX mooncake Nanopb spi_flash app_update fatfs esp_wifi esp_http_client json bt esp_adc usb usb_host_msc driver mbedtls
                    WHOLE_ARCHIVE
                    EMBED_FILES sound/usb_connected.wav sound/usb_disconnected.wav sound/error.wav sound/boot_sound.wav image/boot_logo.png sound/knock.wav sound/morse.wav sound/seagull.wav sound/pum_pam.wav sound/tum_tum.wav sound/eight_bit.wav sound/gps.wav
                    )
                    
                    
nvs_create_partition_image(nvs ../nvs.csv FLASH_IN_PROJECT)