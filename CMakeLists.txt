cmake_minimum_required(VERSION 3.8)
project(breakpad-wla VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)

find_program(NPM_EXECUTABLE npm REQUIRED)
mark_as_advanced(NPM_EXECUTABLE)

if (WIN32)
  set(NPM_PREFIX "cmd" "/c")
endif ()

find_package(LibNetQ REQUIRED)

option(BREAKPAD_INSTALL_SERVER "Install the breakpad server" ON)
option(BREAKPAD_INSTALL_MODULE "Install the breakpad module" ON)
option(BREAKPAD_INSTALL_ASSETS "Install breakpad supporting assets" ON)

set(BREAKPAD_BASE_URL "/" CACHE STRING "URL path prefix the breakpad module is served under (a trailing slash is appended if missing)")
set(BREAKPAD_ROOT_URL "${BREAKPAD_BASE_URL}")
if(NOT BREAKPAD_ROOT_URL MATCHES "/$")
  set(BREAKPAD_ROOT_URL "${BREAKPAD_ROOT_URL}/")
endif()

set(BREAKPAD_ASSETS_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")

set(BREAKPAD_SOURCES
  BreakpadBody.h
  BreakpadBody.c
  BreakpadCType.h
  BreakpadStore.h
  BreakpadStore.c
  BreakpadTool.h
  BreakpadTool.c
  module.c
  )

set(BREAKPAD_DATAFILES
  config.h.in
  )

file(GLOB_RECURSE BREAKPAD_TSXFILES CONFIGURE_DEPENDS
  "src/*.ts"
  "src/*.tsx"
  "src/*.css"
  "public/*"
  "tools/*"
  )

# Absolute, so the outputs of the npm command below and the sources of the
# library name the same files (a relative OUTPUT is taken from the build tree).
set(BREAKPAD_DISTFILES
  "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/dist/manifest.json"
  )

add_custom_command(
  COMMAND ${NPM_PREFIX} "${NPM_EXECUTABLE}" install
  COMMAND ${NPM_PREFIX} "${NPM_EXECUTABLE}" run build --base-url=${BREAKPAD_BASE_URL}
  DEPENDS package.json index.html vite.config.ts ${BREAKPAD_TSXFILES}
  OUTPUT ${BREAKPAD_DISTFILES}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )

configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${BREAKPAD_SOURCES} ${BREAKPAD_DATAFILES} ${BREAKPAD_DISTFILES})
add_library(breakpad_wla SHARED ${BREAKPAD_SOURCES} ${BREAKPAD_DATAFILES} ${BREAKPAD_DISTFILES})
target_include_directories(breakpad_wla PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")
target_link_libraries(breakpad_wla PUBLIC LibNetQ::LibNetQ)
set_property(TARGET breakpad_wla PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(breakpad_wla PROPERTIES PREFIX "" IMPORT_PREFIX "")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES server.c)
add_executable(breakpad_server server.c)
target_link_libraries(breakpad_server PRIVATE LibNetQ::LibNetQ)

if (APPLE)
  set_target_properties(breakpad_server PROPERTIES INSTALL_RPATH "@loader_path/../lib")
elseif (UNIX)
  set_target_properties(breakpad_server PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
  target_link_options(breakpad_server PRIVATE -Wl,--disable-new-dtags)
endif ()

if (BREAKPAD_INSTALL_SERVER)
  install(TARGETS breakpad_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif ()

if (BREAKPAD_INSTALL_MODULE)
  install(TARGETS breakpad_wla
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif ()

if (BREAKPAD_INSTALL_ASSETS)
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/"
    DESTINATION "${BREAKPAD_ASSETS_DIR}"
    )
endif ()
