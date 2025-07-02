idf_component_register(SRCS "ir_learn.c" "ir_encoder.c" "ir_remote.c" "device.c" "main.c" "app_console.c" "register_cmd.c"
                    INCLUDE_DIRS "include"
		    PRIV_INCLUDE_DIRS "priv_include"
		    PRIV_REQUIRES driver esp_timer nvs_flash button console)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
