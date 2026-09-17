set(src_dirs
            LED
            VALUE
            SENSOR
            IIC
            MYMQTT
            MYNVS
            WIFI
            AP_WIFI
            WS_SERVER
            MYBLE
            SCREEN)

set(include_dirs
            BOARD
            LED
            VALUE
            IIC
            SENSOR
            MYMQTT
            MYNVS
            WIFI
            AP_WIFI
            WS_SERVER
            MYBLE
            SCREEN)

set(requires
            spiffs
            esp_http_server
            json
            mqtt
            nvs_flash
            esp_wifi
            bt
            driver)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
