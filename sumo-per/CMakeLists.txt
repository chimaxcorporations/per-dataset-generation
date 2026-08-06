# ns-3 external module: sumo-per
# Place this directory under ns-3-external-contrib/ so ns-3 can discover it.

set(name sumo-per)

set(source_files
  model/sumo-client.cc
  model/per-collector.cc
  model/cam-broadcast-app.cc
  model/cam-header.cc
  helper/sumo-per-helper.cc
)

set(header_files
  model/sumo-client.h
  model/per-collector.h
  model/per-predictor-iface.h
   model/cam-broadcast-app.cc
  model/cam-header.h
  helper/sumo-per-helper.h
)

# ---- SUMO / libsumo discovery ----
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SUMO_PKG QUIET libsumocpp)
endif()


# Fallback: system installs without pkg-config
find_library(LIBSUMOCPP_LIB NAMES sumocpp libsumocpp
             PATHS /usr/lib /usr/lib/x86_64-linux-gnu)

find_path(LIBSUMOCPP_INC
          NAMES libsumo/libsumo.h
          PATHS /usr/include)

if(LIBSUMOCPP_LIB AND LIBSUMOCPP_INC)
  message(STATUS "sumo-per: found libsumocpp via manual fallback")
  add_definitions(-DHAVE_LIBSUMO=1)
  include_directories(${LIBSUMOCPP_INC})
  set(SUMO_LIBS ${LIBSUMOCPP_LIB})
else()
  message(STATUS "sumo-per: libsumocpp not found; building without SUMO coupling (no-SUMO mode).")
  set(SUMO_LIBS "")
endif()

build_lib(
  LIBNAME ${name}
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
 
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libmobility}
    ${libwifi}
    ${libinternet}
    ${libapplications}
    ${SUMO_LIBS}
)

# --- Example program ---

