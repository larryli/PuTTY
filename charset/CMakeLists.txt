include(FindPerl)
if(NOT PERL_EXECUTABLE)
  message(FATAL_ERROR "Perl is required to autogenerate sbcsdat.c")
endif()

set(GENERATED_SBCSDAT_C ${GENERATED_SOURCES_DIR}/sbcsdat.c)
add_custom_command(OUTPUT ${GENERATED_SBCSDAT_C}.tmp
  COMMAND ${PERL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/charset/sbcsgen.pl
    -o ${GENERATED_SBCSDAT_C}.tmp
  DEPENDS ${CMAKE_SOURCE_DIR}/charset/sbcsgen.pl
          ${CMAKE_SOURCE_DIR}/charset/sbcs.dat)
add_custom_target(generated_sbcsdat_c
  BYPRODUCTS ${GENERATED_SBCSDAT_C}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${GENERATED_SBCSDAT_C}.tmp ${GENERATED_SBCSDAT_C}
  DEPENDS ${GENERATED_SBCSDAT_C}.tmp
  COMMENT "Updating sbcsdat.c")

add_library(charset STATIC
  fromucs.c
  localenc.c
  macenc.c
  mimeenc.c
  sbcs.c
  ${GENERATED_SBCSDAT_C}
  slookup.c
  toucs.c
  utf8.c
  xenc.c)
add_dependencies(charset generated_sbcsdat_c)
