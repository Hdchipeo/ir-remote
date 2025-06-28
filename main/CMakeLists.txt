idf_component_register(SRCS "ir-learn.c"
                    INCLUDE_DIRS "."
		    PRIV_REQUIRES nvs_flash driver esp_timer button unity)
