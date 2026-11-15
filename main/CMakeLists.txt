idf_component_register(
    SRCS "sd_card.c" "display.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        fatfs 
        sdmmc 
        driver 
        esp_driver_i2s
        esp_lcd
        esp_driver_gpio
        esp_driver_uart
    PRIV_REQUIRES 
        esp_lcd 
        driver
    )
