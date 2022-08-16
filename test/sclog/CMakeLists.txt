# CMake script for the 'sclog' DynamoRIO instrumentation system that
# goes with the PuTTY test binary 'testsc'. For build instructions see
# the comment at the top of testsc.c.

cmake_minimum_required(VERSION 3.5)

project(sclog LANGUAGES C)

find_package(DynamoRIO)
if (NOT DynamoRIO_FOUND)
  message(FATAL_ERROR "DynamoRIO not found")
endif()

add_library(sclog SHARED sclog.c)
configure_DynamoRIO_client(sclog)
foreach(extension drmgr drsyms drreg drutil drwrap)
  use_DynamoRIO_extension(sclog ${extension})
endforeach()
