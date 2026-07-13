set(src_dirs
            MYIIC
            LCD
            MYSPI
            XL9555
            KEY
            SPI_SD
            TOUCH
            RTC
			MYFATFS
            USART
			)

set(include_dirs
            MYIIC
            LCD
            MYSPI
            XL9555
            KEY
            SPI_SD
            TOUCH
            RTC
			MYFATFS
            USART
			)
            
set(requires
            esp_lcd
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_spi
            esp_driver_sdspi
            esp_driver_sdmmc
            esp_driver_uart
            sdmmc
            fatfs
            esp_libc
            esp_timer
            nvs_flash
            lwip)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format)
