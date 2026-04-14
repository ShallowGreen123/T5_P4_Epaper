set(EPUB_3P_SRCS
    "tinyxml2/tinyxml2.cpp"
    "tjpgd/tjpgd.c"
    "pngdec/PNGdec.cpp"
    "pngdec/adler32.c"
    "pngdec/crc32.c"
    "pngdec/infback.c"
    "pngdec/inffast.c"
    "pngdec/inflate.c"
    "pngdec/inftrees.c"
    "pngdec/zutil.c"
)

idf_component_register(
    SRCS ${EPUB_3P_SRCS}
    INCLUDE_DIRS
        "."
        "miniz"
        "tinyxml2"
        "tjpgd"
        "pngdec"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC __MCUXPRESSO)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-sign-compare
)
