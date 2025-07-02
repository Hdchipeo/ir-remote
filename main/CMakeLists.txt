SET(SOURCE src/ir_learn.c
			src/ir_encoder.c
			src/ir_remote.c
			src/device.c
			src/app_console.c
			src/register_cmd.c
			src/ir_storage.c)

idf_component_register(SRCS ${SOURCE} "main.c"
                    INCLUDE_DIRS "include"
		    PRIV_INCLUDE_DIRS "priv_include"
		    PRIV_REQUIRES driver esp_timer nvs_flash button console spiffs)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
