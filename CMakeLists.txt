cmake_minimum_required(VERSION 3.12)
project(putty LANGUAGES C)

include(cmake/setup.cmake)

add_compile_definitions(HAVE_CMAKE_H)

add_library(utils STATIC
  ${GENERATED_COMMIT_C})
add_dependencies(utils cmake_commit_c)
add_subdirectory(utils)

add_library(logging OBJECT
  logging.c)

add_library(eventloop STATIC
  callback.c timing.c)

add_library(console STATIC
  clicons.c console.c)

add_library(settings STATIC
  cmdline.c settings.c)

add_library(crypto STATIC
  cproxy.c)
add_subdirectory(crypto)

add_library(network STATIC
  be_misc.c nullplug.c errsock.c proxy.c logging.c x11disp.c)

add_library(keygen STATIC
  import.c)
add_subdirectory(keygen)

add_library(agent STATIC
  sshpubk.c pageant.c aqsync.c)

add_library(guiterminal STATIC
  terminal.c ldisc.c minibidi.c config.c dialog.c
  $<TARGET_OBJECTS:logging>)

add_library(noterminal STATIC
  noterm.c ldisc.c)

add_library(all-backends OBJECT
  pinger.c)

add_library(sftpclient STATIC
  psftpcommon.c)
add_subdirectory(ssh)

add_library(otherbackends STATIC
  $<TARGET_OBJECTS:all-backends>
  $<TARGET_OBJECTS:logging>)
add_subdirectory(otherbackends)

add_executable(testcrypt
  testcrypt.c sshpubk.c ssh/crc-attack-detector.c)
target_link_libraries(testcrypt
  keygen crypto utils ${platform_libraries})

add_executable(test_host_strfoo
  utils/host_strchr_internal.c)
target_compile_definitions(test_host_strfoo PRIVATE TEST)
target_link_libraries(test_host_strfoo utils ${platform_libraries})

add_executable(test_tree234
  utils/tree234.c)
target_compile_definitions(test_tree234 PRIVATE TEST)
target_link_libraries(test_tree234 utils ${platform_libraries})

add_executable(test_wildcard
  utils/wildcard.c)
target_compile_definitions(test_wildcard PRIVATE TEST)
target_link_libraries(test_wildcard utils ${platform_libraries})

foreach(subdir ${PLATFORM_SUBDIRS})
  add_subdirectory(${subdir})
endforeach()

configure_file(cmake/cmake.h.in ${GENERATED_SOURCES_DIR}/cmake.h)
