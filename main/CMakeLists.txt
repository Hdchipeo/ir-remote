SET(SOURCE src/ir_learn.c
			src/ir_encoder.c
			src/register_cmd.c
			src/ir_storage.c
			src/ir.c)

idf_component_register(SRCS ${SOURCE} "main.c" "app_ir.c" "app_console.c" "app_driver.c" "app_espnow.c" "app_web_server.c"
                    INCLUDE_DIRS "include"
		    PRIV_INCLUDE_DIRS "priv_include"
		    PRIV_REQUIRES driver esp_timer nvs_flash button console spiffs esp_netif esp_wifi esp_http_server mdns)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")

spiffs_create_partition_image(storage storage FLASH_IN_PROJECT)
