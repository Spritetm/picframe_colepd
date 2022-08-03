set(srcs "main.c" "epd.c" "sync.c")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
