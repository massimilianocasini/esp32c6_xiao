# CMakeLists.txt for OLED Display Component
#
# This component provides SSD1306 OLED display driver functionality
# using the ESP-IDF I2C driver.

idf_component_register(
    SRCS
        "oled_display.c"
        "ssd1306.c"
        "font_8x8.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        driver
        esp_timer
        freertos
)
