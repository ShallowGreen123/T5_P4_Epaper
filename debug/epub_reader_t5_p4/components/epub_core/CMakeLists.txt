set(EPUB_CORE_SRCS
    "EpubList/Epub.cpp"
    "EpubList/EpubList.cpp"
    "EpubList/EpubReader.cpp"
    "EpubList/EpubToc.cpp"
    "Renderer/JPEGHelper.cpp"
    "Renderer/PNGHelper.cpp"
    "Renderer/Renderer.cpp"
    "RubbishHtmlParser/blocks/TextBlock.cpp"
    "RubbishHtmlParser/htmlEntities.cpp"
    "RubbishHtmlParser/RubbishHtmlParser.cpp"
    "ZipFile/ZipFile.cpp"
)

idf_component_register(
    SRCS ${EPUB_CORE_SRCS}
    INCLUDE_DIRS "."
    REQUIRES epub_3p
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-unused-variable
)
