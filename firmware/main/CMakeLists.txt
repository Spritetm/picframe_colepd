set(srcs "main.c"
    "epd.c"
    )

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
