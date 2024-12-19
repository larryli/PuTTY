# Look for GTK, of any version.

set(PUTTY_GTK_VERSION "ANY"
  CACHE STRING "Which major version of GTK to build with")
set_property(CACHE PUTTY_GTK_VERSION
  PROPERTY STRINGS ANY 3 2 1 NONE)

set(GTK_FOUND FALSE)

macro(try_pkg_config_gtk VER PACKAGENAME)
  if(NOT GTK_FOUND AND
      (PUTTY_GTK_VERSION STREQUAL ANY OR PUTTY_GTK_VERSION STREQUAL ${VER}))
    find_package(PkgConfig)
    pkg_check_modules(GTK ${PACKAGENAME})
    if(GTK_FOUND)
      set(GTK_VERSION ${VER})
    endif()
  endif()
endmacro()
try_pkg_config_gtk(3 gtk+-3.0)
try_pkg_config_gtk(2 gtk+-2.0)

if(NOT GTK_FOUND AND
    (PUTTY_GTK_VERSION STREQUAL ANY OR PUTTY_GTK_VERSION STREQUAL 1))
  message("-- Checking for GTK1 (via gtk-config)")
  find_program(GTK_CONFIG gtk-config)
  if(GTK_CONFIG)
    execute_process(COMMAND ${GTK_CONFIG} --cflags
      OUTPUT_VARIABLE gtk_config_cflags
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE gtk_config_cflags_result)
    execute_process(COMMAND ${GTK_CONFIG} --libs
      OUTPUT_VARIABLE gtk_config_libs
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE gtk_config_libs_result)

    if(gtk_config_cflags_result EQUAL 0 AND gtk_config_libs_result EQUAL 0)

      set(GTK_INCLUDE_DIRS)
      set(GTK_LIBRARY_DIRS)
      set(GTK_LIBRARIES)

      separate_arguments(gtk_config_cflags NATIVE_COMMAND
        ${gtk_config_cflags})
      foreach(opt ${gtk_config_cflags})
        string(REGEX MATCH "^-I" ok ${opt})
        if(ok)
          string(REGEX REPLACE "^-I" "" optval ${opt})
          list(APPEND GTK_INCLUDE_DIRS ${optval})
        endif()
      endforeach()

      separate_arguments(gtk_config_libs NATIVE_COMMAND
        ${gtk_config_libs})
      foreach(opt ${gtk_config_libs})
        string(REGEX MATCH "^-l" ok ${opt})
        if(ok)
          list(APPEND GTK_LIBRARIES ${opt})
        endif()
        string(REGEX MATCH "^-L" ok ${opt})
        if(ok)
          string(REGEX REPLACE "^-L" "" optval ${opt})
          list(APPEND GTK_LIBRARY_DIRS ${optval})
        endif()
      endforeach()

      message("--   Found GTK1")
      set(GTK_FOUND TRUE)
    endif()
  endif()
endif()

if(GTK_FOUND)
  # Check for some particular Pango functions.
  function(pango_check_subscope)
    set(CMAKE_REQUIRED_INCLUDES ${GTK_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LINK_OPTIONS ${GTK_LDFLAGS})
    set(CMAKE_REQUIRED_LIBRARIES ${GTK_LIBRARIES})
    check_symbol_exists(pango_font_family_is_monospace "pango/pango.h"
      HAVE_PANGO_FONT_FAMILY_IS_MONOSPACE)
    check_symbol_exists(pango_font_map_list_families "pango/pango.h"
      HAVE_PANGO_FONT_MAP_LIST_FAMILIES)
    set(HAVE_PANGO_FONT_FAMILY_IS_MONOSPACE
      ${HAVE_PANGO_FONT_FAMILY_IS_MONOSPACE} PARENT_SCOPE)
    set(HAVE_PANGO_FONT_MAP_LIST_FAMILIES
      ${HAVE_PANGO_FONT_MAP_LIST_FAMILIES} PARENT_SCOPE)
    check_c_source_compiles("
      #include <gtk/gtk.h>
      int f = G_APPLICATION_DEFAULT_FLAGS;
      int main(void) {}" HAVE_G_APPLICATION_DEFAULT_FLAGS)
  endfunction()
  pango_check_subscope()
endif()
