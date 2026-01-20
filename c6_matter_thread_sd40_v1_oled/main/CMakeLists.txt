# Main component CMakeLists.txt for Matter SCD40 OLED project

idf_component_register(
    SRC_DIRS
        "."
    INCLUDE_DIRS
        "."
        "include"
    PRIV_REQUIRES
        nvs_flash
        driver
        esp_event
        esp_timer
        freertos
        esp_netif
        openthread
        chip
        esp_matter
        esp_matter_console
        oled_display
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
