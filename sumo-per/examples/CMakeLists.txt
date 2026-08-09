build_lib_example(
    NAME sumo-per-example

    SOURCE_FILES
        sumo-per-example.cc

    LIBRARIES_TO_LINK
        ${libsumo-per}
)