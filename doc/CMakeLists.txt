cmake_minimum_required(VERSION 3.7)
project(putty-documentation LANGUAGES)

# This build script can be run standalone, or included as a
# subdirectory of the main PuTTY cmake build system. If the latter, a
# couple of things change: it has to set variables telling the rest of
# the build system what manpages are available to be installed, and it
# will change whether the 'make doc' target is included in 'make all'.

include(FindGit)
include(FindPerl)
find_program(HALIBUT halibut)

set(doc_outputs)
set(manpage_outputs)

if(HALIBUT AND PERL_EXECUTABLE)
  # Build the main manual, which requires not only Halibut, but also
  # Perl to run licence.pl to generate the copyright and licence
  # sections from the master data outside this directory.

  # If this is a source archive in which a fixed version.but was
  # provided, use that. Otherwise, infer one from the git checkout (if
  # possible).

  set(manual_dependencies) # extra target names to depend on

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/version.but)
    set(VERSION_BUT ${CMAKE_CURRENT_SOURCE_DIR}/version.but)
  else()
    set(VERSION_BUT ${CMAKE_CURRENT_BINARY_DIR}/cmake_version.but)
    set(INTERMEDIATE_VERSION_BUT ${VERSION_BUT}.tmp)
    add_custom_target(check_git_commit_for_doc
      BYPRODUCTS ${INTERMEDIATE_VERSION_BUT}
      COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DOUTPUT_FILE=${INTERMEDIATE_VERSION_BUT}
        -DOUTPUT_TYPE=halibut
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/gitcommit.cmake
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/gitcommit.cmake
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      COMMENT "Checking current git commit")
    add_custom_target(cmake_version_but
      BYPRODUCTS ${VERSION_BUT}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${INTERMEDIATE_VERSION_BUT} ${VERSION_BUT}
      DEPENDS check_git_commit_for_doc ${INTERMEDIATE_VERSION_BUT}
      COMMENT "Updating cmake_version.but")
    set(manual_dependencies ${manual_dependencies} cmake_version_but)
  endif()

  add_custom_target(copy_but
    BYPRODUCTS copy.but
    COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../licence.pl
      --copyrightdoc -o copy.but
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../licence.pl ${CMAKE_CURRENT_SOURCE_DIR}/../LICENCE)
  add_custom_target(licence_but
    BYPRODUCTS licence.but
    COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../licence.pl
      --licencedoc -o licence.but
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../licence.pl ${CMAKE_CURRENT_SOURCE_DIR}/../LICENCE)
  set(manual_dependencies ${manual_dependencies} copy_but licence_but)

  set(manual_sources
    ${CMAKE_CURRENT_BINARY_DIR}/copy.but
    ${CMAKE_CURRENT_SOURCE_DIR}/blurb.but
    ${CMAKE_CURRENT_SOURCE_DIR}/intro.but
    ${CMAKE_CURRENT_SOURCE_DIR}/gs.but
    ${CMAKE_CURRENT_SOURCE_DIR}/using.but
    ${CMAKE_CURRENT_SOURCE_DIR}/config.but
    ${CMAKE_CURRENT_SOURCE_DIR}/pscp.but
    ${CMAKE_CURRENT_SOURCE_DIR}/psftp.but
    ${CMAKE_CURRENT_SOURCE_DIR}/plink.but
    ${CMAKE_CURRENT_SOURCE_DIR}/pubkey.but
    ${CMAKE_CURRENT_SOURCE_DIR}/pageant.but
    ${CMAKE_CURRENT_SOURCE_DIR}/errors.but
    ${CMAKE_CURRENT_SOURCE_DIR}/faq.but
    ${CMAKE_CURRENT_SOURCE_DIR}/feedback.but
    ${CMAKE_CURRENT_SOURCE_DIR}/pubkeyfmt.but
    ${CMAKE_CURRENT_BINARY_DIR}/licence.but
    ${CMAKE_CURRENT_SOURCE_DIR}/udp.but
    ${CMAKE_CURRENT_SOURCE_DIR}/pgpkeys.but
    ${CMAKE_CURRENT_SOURCE_DIR}/sshnames.but
    ${CMAKE_CURRENT_SOURCE_DIR}/authplugin.but
    ${CMAKE_CURRENT_SOURCE_DIR}/index.but
    ${VERSION_BUT})

  # The HTML manual goes in a subdirectory, for convenience.
  set(html_dir ${CMAKE_CURRENT_BINARY_DIR}/html)
  file(MAKE_DIRECTORY ${html_dir})
  add_custom_command(OUTPUT ${html_dir}/index.html
    COMMAND ${HALIBUT} --html ${manual_sources}
    WORKING_DIRECTORY ${html_dir}
    DEPENDS ${manual_sources} ${manual_dependencies})
  list(APPEND doc_outputs ${html_dir}/index.html)

  # Windows help.
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/chmextra.but
    "\\cfg{chm-extra-file}{${CMAKE_CURRENT_SOURCE_DIR}/chm.css}{chm.css}\n")
  add_custom_command(OUTPUT putty.chm
    COMMAND ${HALIBUT} --chm chmextra.but ${manual_sources}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${manual_sources} ${manual_dependencies})
  list(APPEND doc_outputs putty.chm)

  # Plain text.
  add_custom_command(OUTPUT puttydoc.txt
    COMMAND ${HALIBUT} --text ${manual_sources}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${manual_sources} ${manual_dependencies})
  list(APPEND doc_outputs puttydoc.txt)
endif()

macro(register_manpage title section)
  list(APPEND manpage_outputs ${title}.${section})
  if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Only set this variable if there _is_ a parent scope.
    set(HAVE_MANPAGE_${title}_${section} ON PARENT_SCOPE)
  endif()
endmacro()

if(NOT HALIBUT)
  # If we don't have Halibut available to rebuild the man pages from
  # source, we must check whether the build and source directories
  # correspond, so as to suppress the build rules that copy them from
  # the source dir to the build dir. (Otherwise, someone unpacking
  # putty-src.zip and building on a system without Halibut will find
  # that there's a circular dependency in the makefile, which at least
  # Ninja complains about.)
  get_filename_component(DOCBUILDDIR ${CMAKE_CURRENT_BINARY_DIR} REALPATH)
  get_filename_component(DOCSRCDIR   ${CMAKE_CURRENT_SOURCE_DIR} REALPATH)
endif()

macro(manpage title section)
  if(HALIBUT)
    add_custom_command(OUTPUT ${title}.${section}
      COMMAND ${HALIBUT} --man=${title}.${section}
        ${CMAKE_CURRENT_SOURCE_DIR}/mancfg.but
        ${CMAKE_CURRENT_SOURCE_DIR}/man-${title}.but
      DEPENDS
        mancfg.but man-${title}.but)
    register_manpage(${title} ${section})
  elseif(NOT (DOCBUILDDIR STREQUAL DOCSRCDIR)
         AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${title}.${section})
    add_custom_command(OUTPUT ${title}.${section}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/${title}.${section} ${title}.${section}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${title}.${section})
    register_manpage(${title} ${section})
  endif()
endmacro()

manpage(putty 1)
manpage(puttygen 1)
manpage(plink 1)
manpage(pscp 1)
manpage(psftp 1)
manpage(puttytel 1)
manpage(pterm 1)
manpage(pageant 1)
manpage(psocks 1)
manpage(psusan 1)

add_custom_target(manpages ALL DEPENDS ${manpage_outputs})
add_custom_target(doc DEPENDS ${doc_outputs} manpages)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # If we're doing a cmake from just the doc subdir, we expect the
  # user to want to make all the documentation, including HTML and so
  # forth. (What else would be the point?)
  #
  # But if we're included from the main makefile, then by default we
  # only make the man pages (which are necessary for 'make install'),
  # and we leave everything else to a separate 'make doc' target which
  # the user can invoke if they need to.
  add_custom_target(doc-default ALL DEPENDS doc)
endif()
