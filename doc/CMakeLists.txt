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
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/version.but)
    set(VERSION_BUT ${CMAKE_CURRENT_SOURCE_DIR}/version.but)
  else()
    set(VERSION_BUT ${CMAKE_CURRENT_BINARY_DIR}/cmake_version.but)
    set(INTERMEDIATE_VERSION_BUT ${VERSION_BUT}.tmp)
    add_custom_target(check_git_commit_for_doc
      BYPRODUCTS ${INTERMEDIATE_VERSION_BUT}
      COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DTOPLEVEL_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_FILE=${INTERMEDIATE_VERSION_BUT}
        -DOUTPUT_TYPE=halibut
        -P ${CMAKE_SOURCE_DIR}/cmake/gitcommit.cmake
      DEPENDS ${CMAKE_SOURCE_DIR}/cmake/gitcommit.cmake
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Checking current git commit")
    add_custom_target(cmake_version_but
      BYPRODUCTS ${VERSION_BUT}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${INTERMEDIATE_VERSION_BUT} ${VERSION_BUT}
      DEPENDS check_git_commit_for_doc ${INTERMEDIATE_VERSION_BUT}
      COMMENT "Updating cmake_version.but")
  endif()

  add_custom_command(OUTPUT copy.but
    COMMAND ${PERL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/licence.pl
      --copyrightdoc -o copy.but
    DEPENDS ${CMAKE_SOURCE_DIR}/licence.pl ${CMAKE_SOURCE_DIR}/LICENCE)
  add_custom_command(OUTPUT licence.but
    COMMAND ${PERL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/licence.pl
      --licencedoc -o licence.but
    DEPENDS ${CMAKE_SOURCE_DIR}/licence.pl ${CMAKE_SOURCE_DIR}/LICENCE)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/index.but
    ${VERSION_BUT})

  # The HTML manual goes in a subdirectory, for convenience.
  set(html_dir ${CMAKE_CURRENT_BINARY_DIR}/html)
  file(MAKE_DIRECTORY ${html_dir})
  add_custom_command(OUTPUT ${html_dir}/index.html
    COMMAND ${HALIBUT} --html ${manual_sources}
    WORKING_DIRECTORY ${html_dir}
    DEPENDS ${manual_sources})
  list(APPEND doc_outputs ${html_dir}/index.html)

  # Windows help.
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/chmextra.but
    "\\cfg{chm-extra-file}{${CMAKE_CURRENT_SOURCE_DIR}/chm.css}{chm.css}\n")
  add_custom_command(OUTPUT putty.chm
    COMMAND ${HALIBUT} --chm chmextra.but ${manual_sources}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${manual_sources})
  list(APPEND doc_outputs putty.chm)

  # Plain text.
  add_custom_command(OUTPUT puttydoc.txt
    COMMAND ${HALIBUT} --text ${manual_sources}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${manual_sources})
  list(APPEND doc_outputs puttydoc.txt)
endif()

macro(manpage title section)
  if(HALIBUT)
    add_custom_command(OUTPUT ${title}.${section}
      COMMAND ${HALIBUT} --man=${title}.${section}
        ${CMAKE_CURRENT_SOURCE_DIR}/mancfg.but
        ${CMAKE_CURRENT_SOURCE_DIR}/man-${title}.but
      DEPENDS
        mancfg.but man-${title}.but)
    list(APPEND manpage_outputs ${title}.${section})
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

add_custom_target(manpages ALL
  DEPENDS ${manpage_outputs})
add_custom_target(doc
  DEPENDS ${doc_outputs} manpages)
