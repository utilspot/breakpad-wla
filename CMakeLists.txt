cmake_minimum_required(VERSION 3.18)
project(breakpad-wla VERSION 1.1.0 LANGUAGES C CXX)

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

# Breakpad as static libraries, from the install prefix: libbreakpad.a (the
# processor, with the stackwalk printing code), plus libbreakpad_dump_syms.a
# and libdisasm.a, which bitmake.config.mjs packs from Breakpad's build tree
# (Breakpad's own install leaves them out).
find_path(BREAKPAD_INCLUDE_DIR google_breakpad/processor/minidump.h PATH_SUFFIXES breakpad REQUIRED)
find_library(BREAKPAD_PROCESSOR_LIBRARY NAMES libbreakpad.a REQUIRED)
find_library(BREAKPAD_DUMP_SYMS_LIBRARY NAMES libbreakpad_dump_syms.a REQUIRED)
find_library(BREAKPAD_DISASM_LIBRARY NAMES libdisasm.a REQUIRED)
find_package(ZLIB REQUIRED)

# The stdio calls Breakpad's libraries print with; BreakpadStdio.c wraps each
# so the output goes to the request running the tool.
set(BREAKPAD_WRAPPED_STDIO
  printf __printf_chk vprintf __vprintf_chk
  fprintf __fprintf_chk vfprintf __vfprintf_chk
  puts fputs putchar putc fputc fwrite
  )

set(BREAKPAD_DATA_DIR "" CACHE STRING "Absolute directory owned by this service only; its tmp/ is emptied on start (empty: breakpad-data/ in the server's working directory)")

set(BREAKPAD_SOURCES
  BreakpadBody.h
  BreakpadBody.c
  BreakpadCType.h
  BreakpadStore.h
  BreakpadStore.c
  BreakpadTool.h
  BreakpadTool.c
  BreakpadBuiltin.h
  BreakpadBuiltin.cpp
  BreakpadStdio.c
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
target_include_directories(breakpad_wla PRIVATE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
  "${BREAKPAD_INCLUDE_DIR}"
  )
target_compile_features(breakpad_wla PRIVATE cxx_std_17)
target_link_libraries(breakpad_wla
  PUBLIC LibNetQ::LibNetQ
  PRIVATE ${BREAKPAD_DUMP_SYMS_LIBRARY} ${BREAKPAD_PROCESSOR_LIBRARY} ${BREAKPAD_DISASM_LIBRARY} ZLIB::ZLIB
  )
list(TRANSFORM BREAKPAD_WRAPPED_STDIO PREPEND "-Wl,--wrap=" OUTPUT_VARIABLE BREAKPAD_WRAP_OPTIONS)
target_link_options(breakpad_wla PRIVATE ${BREAKPAD_WRAP_OPTIONS})
if (UNIX AND NOT APPLE)
  # Every Breakpad symbol the tools need must be in the module, not left for
  # dlopen to miss; and none of the static libraries' symbols are exported.
  target_link_options(breakpad_wla PRIVATE -Wl,--no-undefined -Wl,--exclude-libs,ALL)
endif ()
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
