set(hello "${MOSAICO_HELLO}/components/app_hello")
idf_component_register(SRCS "main.c" "${hello}/app_hello.c"
    PRIV_INCLUDE_DIRS "${hello}/include"
    PRIV_REQUIRES app_display esp-gsp log okou_agent)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${hello}/ui/main.json"
    PIXEL_FORMAT rgb565)
