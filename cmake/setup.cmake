# Forcibly re-enable assertions, even if we're building in release
# mode. This is a security project - assertions may be enforcing
# security-critical constraints. A backstop #ifdef in defs.h should
# give a #error if this manoeuvre doesn't do what it needs to.
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(PUTTY_IPV6 ON
  CACHE BOOL "Build PuTTY with IPv6 support if possible")
set(PUTTY_DEBUG OFF
  CACHE BOOL "Build PuTTY with debug() statements enabled")
set(PUTTY_FUZZING OFF
  CACHE BOOL "Build PuTTY binaries suitable for fuzzing, NOT FOR REAL USE")
set(PUTTY_COVERAGE OFF
  CACHE BOOL "Build PuTTY binaries suitable for code coverage analysis")

set(STRICT OFF
  CACHE BOOL "Enable extra compiler warnings and make them errors")

include(FindGit)

set(GENERATED_SOURCES_DIR ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY})

set(GENERATED_LICENCE_H ${GENERATED_SOURCES_DIR}/licence.h)
set(INTERMEDIATE_LICENCE_H ${GENERATED_LICENCE_H}.tmp)
add_custom_command(OUTPUT ${INTERMEDIATE_LICENCE_H}
  COMMAND ${CMAKE_COMMAND}
    -DLICENCE_FILE=${CMAKE_SOURCE_DIR}/LICENCE
    -DOUTPUT_FILE=${INTERMEDIATE_LICENCE_H}
    -P ${CMAKE_SOURCE_DIR}/cmake/licence.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/licence.cmake ${CMAKE_SOURCE_DIR}/LICENCE)
add_custom_target(generated_licence_h
  BYPRODUCTS ${GENERATED_LICENCE_H}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${INTERMEDIATE_LICENCE_H} ${GENERATED_LICENCE_H}
  DEPENDS ${INTERMEDIATE_LICENCE_H}
  COMMENT "Updating licence.h")

set(GENERATED_COMMIT_C ${GENERATED_SOURCES_DIR}/cmake_commit.c)
set(INTERMEDIATE_COMMIT_C ${GENERATED_COMMIT_C}.tmp)
add_custom_target(check_git_commit
  BYPRODUCTS ${INTERMEDIATE_COMMIT_C}
  COMMAND ${CMAKE_COMMAND}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
    -DTOPLEVEL_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DOUTPUT_FILE=${INTERMEDIATE_COMMIT_C}
    -P ${CMAKE_SOURCE_DIR}/cmake/gitcommit.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/gitcommit.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking current git commit")
add_custom_target(cmake_commit_c
  BYPRODUCTS ${GENERATED_COMMIT_C}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${INTERMEDIATE_COMMIT_C} ${GENERATED_COMMIT_C}
  DEPENDS check_git_commit ${INTERMEDIATE_COMMIT_C}
  COMMENT "Updating cmake_commit.c")

function(add_platform_sources_to_library target)
  set(sources ${ARGN})
  list(TRANSFORM sources PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
  target_sources(${target} PRIVATE ${sources})
endfunction()

if(CMAKE_SYSTEM_NAME MATCHES "Windows" OR WINELIB)
  include(cmake/platforms/windows.cmake)
else()
  include(cmake/platforms/unix.cmake)
endif()

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GENERATED_SOURCES_DIR}
  ${PLATFORM_SUBDIRS})

if(PUTTY_DEBUG)
  add_compile_definitions(DEBUG)
endif()
if(PUTTY_FUZZING)
  add_compile_definitions(FUZZING)
endif()
if(PUTTY_COVERAGE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage -g ")
endif()
