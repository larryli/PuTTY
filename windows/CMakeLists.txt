set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  utils/arm_arch_queries.c
  utils/cryptoapi.c
  utils/defaults.c
  utils/dll_hijacking_protection.c
  utils/dputs.c
  utils/escape_registry_key.c
  utils/filename.c
  utils/fontspec.c
  utils/getdlgitemtext_alloc.c
  utils/get_username.c
  utils/is_console_handle.c
  utils/load_system32_dll.c
  utils/ltime.c
  utils/makedlgitemborderless.c
  utils/message_box.c
  utils/minefield.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints_msgbox.c
  utils/platform_get_x_display.c
  utils/registry_get_string.c
  utils/request_file.c
  utils/security.c
  utils/split_into_argv.c
  utils/version.c
  utils/win_strerror.c
  unicode.c)
if(NOT HAVE_STRTOUMAX)
  add_sources_from_current_dir(utils utils/strtoumax.c)
endif()
add_sources_from_current_dir(eventloop
  cliloop.c handle-io.c)
add_sources_from_current_dir(console
  select-cli.c nohelp.c console.c)
add_sources_from_current_dir(settings
  storage.c)
add_sources_from_current_dir(network
  network.c handle-socket.c named-pipe-client.c named-pipe-server.c local-proxy.c x11.c)
add_sources_from_current_dir(sshcommon
  noise.c)
add_sources_from_current_dir(sshclient
  agent-client.c gss.c sharing.c)
add_sources_from_current_dir(sftpclient
  sftp.c)
add_sources_from_current_dir(otherbackends
  serial.c)
add_sources_from_current_dir(agent
  agent-client.c)
add_sources_from_current_dir(guiterminal
  dialog.c controls.c config.c printing.c jump-list.c sizetip.c)
add_dependencies(guiterminal generated_licence_h) # dialog.c uses licence.h

add_library(guimisc STATIC
  select-gui.c)

add_executable(pageant
  pageant.c
  help.c
  pageant.rc)
add_dependencies(pageant generated_licence_h)
target_link_libraries(pageant
  guimisc eventloop agent network crypto utils
  ${platform_libraries})
set_target_properties(pageant PROPERTIES
  WIN32_EXECUTABLE ON
  LINK_FLAGS "${LFLAG_MANIFEST_NO}")
installed_program(pageant)

add_executable(plink
  plink.c
  ${CMAKE_SOURCE_DIR}/be_all_s.c
  no-jump-list.c
  nohelp.c
  plink.rc)
add_dependencies(plink generated_licence_h)
target_link_libraries(plink
  eventloop console noterminal sshclient otherbackends settings network crypto
  utils
  ${platform_libraries})
installed_program(plink)

add_executable(pscp
  ${CMAKE_SOURCE_DIR}/pscp.c
  ${CMAKE_SOURCE_DIR}/be_ssh.c
  no-jump-list.c
  nohelp.c
  pscp.rc)
add_dependencies(pscp generated_licence_h)
target_link_libraries(pscp
  sftpclient eventloop console sshclient settings network crypto utils
  ${platform_libraries})
installed_program(pscp)

add_executable(psftp
  ${CMAKE_SOURCE_DIR}/psftp.c
  ${CMAKE_SOURCE_DIR}/be_ssh.c
  no-jump-list.c
  nohelp.c
  psftp.rc)
add_dependencies(psftp generated_licence_h)
target_link_libraries(psftp
  sftpclient eventloop console sshclient settings network crypto utils
  ${platform_libraries})
installed_program(psftp)

add_executable(psocks
  psocks.c
  nohelp.c
  ${CMAKE_SOURCE_DIR}/psocks.c
  ${CMAKE_SOURCE_DIR}/norand.c
  ${CMAKE_SOURCE_DIR}/nocproxy.c
  ${CMAKE_SOURCE_DIR}/ssh/portfwd.c)
target_link_libraries(psocks
  eventloop console network utils
  ${platform_libraries})

add_executable(putty
  window.c
  help.c
  ${CMAKE_SOURCE_DIR}/be_all_s.c
  putty.rc)
add_dependencies(putty generated_licence_h)
target_link_libraries(putty
  guiterminal guimisc eventloop sshclient otherbackends settings network crypto
  utils
  ${platform_libraries})
set_target_properties(putty PROPERTIES
  WIN32_EXECUTABLE ON
  LINK_FLAGS "${LFLAG_MANIFEST_NO}")
installed_program(putty)

add_executable(puttytel
  window.c
  help.c
  ${CMAKE_SOURCE_DIR}/be_nos_s.c
  ${CMAKE_SOURCE_DIR}/nogss.c
  ${CMAKE_SOURCE_DIR}/norand.c
  ${CMAKE_SOURCE_DIR}/nocproxy.c
  puttytel.rc)
add_dependencies(puttytel generated_licence_h)
target_link_libraries(puttytel
  guiterminal guimisc eventloop otherbackends settings network utils
  ${platform_libraries})
set_target_properties(puttytel PROPERTIES
  WIN32_EXECUTABLE ON
  LINK_FLAGS "${LFLAG_MANIFEST_NO}")
installed_program(puttytel)

add_executable(puttygen
  puttygen.c
  ${CMAKE_SOURCE_DIR}/notiming.c
  noise.c
  no-jump-list.c
  storage.c
  help.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c
  controls.c
  puttygen.rc)
add_dependencies(puttygen generated_licence_h)
target_link_libraries(puttygen
  keygen guimisc crypto utils
  ${platform_libraries})
set_target_properties(puttygen PROPERTIES
  WIN32_EXECUTABLE ON
  LINK_FLAGS "${LFLAG_MANIFEST_NO}")
installed_program(puttygen)

add_executable(test_split_into_argv
  utils/split_into_argv.c)
target_compile_definitions(test_split_into_argv PRIVATE TEST)
target_link_libraries(test_split_into_argv utils ${platform_libraries})
