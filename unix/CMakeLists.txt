set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  utils/arm_arch_queries.c
  utils/block_signal.c
  utils/cloexec.c
  utils/dputs.c
  utils/filename.c
  utils/fontspec.c
  utils/getticks.c
  utils/get_username.c
  utils/keysym_to_unicode.c
  utils/make_dir_and_check_ours.c
  utils/make_dir_path.c
  utils/nonblock.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints.c
  utils/pollwrap.c
  utils/signal.c
  utils/x11_ignore_error.c
  # Compiled icon pixmap files
  xpmpucfg.c xpmputty.c xpmptcfg.c xpmpterm.c
  # We want the ISO C implementation of ltime(), because we don't have
  # a local better alternative
  ../utils/ltime.c)
add_sources_from_current_dir(eventloop
  uxcliloop.c uxsel.c)
add_sources_from_current_dir(console
  uxcons.c)
add_sources_from_current_dir(settings
  uxstore.c)
add_sources_from_current_dir(network
  uxnet.c uxfdsock.c uxagentsock.c uxpeer.c uxproxy.c ux_x11.c)
add_sources_from_current_dir(sshcommon
  uxnoise.c)
add_sources_from_current_dir(sshclient
  uxgss.c uxagentc.c uxshare.c)
add_sources_from_current_dir(sshserver
  uxsftpserver.c procnet.c)
add_sources_from_current_dir(sftpclient
  uxsftp.c)
add_sources_from_current_dir(otherbackends
  uxser.c)
add_sources_from_current_dir(agent
  uxagentc.c)

add_executable(fuzzterm
  ${CMAKE_SOURCE_DIR}/fuzzterm.c
  ${CMAKE_SOURCE_DIR}/be_none.c
  ${CMAKE_SOURCE_DIR}/logging.c
  ${CMAKE_SOURCE_DIR}/noprint.c
  uxucs.c
  uxnogtk.c)
add_dependencies(fuzzterm generated_licence_h)
target_link_libraries(fuzzterm
  guiterminal eventloop charset settings utils)

add_executable(osxlaunch
  osxlaunch.c)

add_executable(plink
  uxplink.c
  ${CMAKE_SOURCE_DIR}/be_all_s.c
  uxnogtk.c)
target_link_libraries(plink
  eventloop noterminal console sshclient otherbackends settings network crypto
  utils)
installed_program(plink)

add_executable(pscp
  ${CMAKE_SOURCE_DIR}/pscp.c
  ${CMAKE_SOURCE_DIR}/be_ssh.c
  uxnogtk.c)
target_link_libraries(pscp
  sftpclient eventloop console sshclient settings network crypto utils)
installed_program(pscp)

add_executable(psftp
  ${CMAKE_SOURCE_DIR}/psftp.c
  ${CMAKE_SOURCE_DIR}/be_ssh.c
  uxnogtk.c)
target_link_libraries(psftp
  sftpclient eventloop console sshclient settings network crypto utils)
installed_program(psftp)

add_executable(psocks
  uxsocks.c
  ${CMAKE_SOURCE_DIR}/psocks.c
  ${CMAKE_SOURCE_DIR}/norand.c
  ${CMAKE_SOURCE_DIR}/nocproxy.c
  ${CMAKE_SOURCE_DIR}/ssh/portfwd.c
  uxnogtk.c)
target_link_libraries(psocks
  eventloop console network utils)

add_executable(psusan
  uxpsusan.c
  ${CMAKE_SOURCE_DIR}/be_none.c
  ${CMAKE_SOURCE_DIR}/nogss.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  uxnogtk.c
  uxpty.c)
target_link_libraries(psusan
  eventloop sshserver keygen settings network crypto utils)
installed_program(psusan)

add_library(puttygen-common OBJECT
  ${CMAKE_SOURCE_DIR}/notiming.c
  uxgen.c
  uxnogtk.c
  uxnoise.c
  uxstore.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c)

add_executable(puttygen
  ${CMAKE_SOURCE_DIR}/cmdgen.c)
target_link_libraries(puttygen
  puttygen-common keygen console crypto utils)
installed_program(puttygen)

add_executable(cgtest
  ${CMAKE_SOURCE_DIR}/cgtest.c)
target_link_libraries(cgtest
  puttygen-common keygen console crypto utils)

add_executable(testsc
  ${CMAKE_SOURCE_DIR}/testsc.c)
target_link_libraries(testsc crypto utils)

add_executable(testzlib
  ${CMAKE_SOURCE_DIR}/testzlib.c
  ${CMAKE_SOURCE_DIR}/ssh/zlib.c)
target_link_libraries(testzlib utils)

add_executable(uppity
  uxserver.c
  ${CMAKE_SOURCE_DIR}/be_none.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  uxnogtk.c
  uxpty.c
  ${CMAKE_SOURCE_DIR}/nogss.c)
target_link_libraries(uppity
  eventloop sshserver keygen settings network crypto utils)

if(GTK_FOUND)
  add_sources_from_current_dir(utils
    gtkcols.c)
  add_sources_from_current_dir(guiterminal
    gtkwin.c gtkfont.c gtkdlg.c gtkcfg.c gtkcomm.c uxcfg.c uxucs.c uxprint.c)
  add_dependencies(guiterminal generated_licence_h) # gtkdlg.c uses licence.h

  add_library(guimisc STATIC
    gtkmisc.c)

  add_executable(pageant
    uxpgnt.c
    ${CMAKE_SOURCE_DIR}/be_misc.c
    ${CMAKE_SOURCE_DIR}/be_none.c
    ${CMAKE_SOURCE_DIR}/nogss.c
    gtkask.c
    ux_x11.c
    uxnoise.c
    ${CMAKE_SOURCE_DIR}/ssh/x11fwd.c)
  target_link_libraries(pageant
    guimisc eventloop console agent settings network crypto utils
    ${GTK_LIBRARIES})
  installed_program(pageant)

  add_executable(pterm
    uxpterm.c
    gtkmain.c
    ${CMAKE_SOURCE_DIR}/be_none.c
    ${CMAKE_SOURCE_DIR}/nogss.c
    uxpty.c)
  target_link_libraries(pterm
    guiterminal guimisc eventloop settings charset utils
    ${GTK_LIBRARIES} ${X11_LIBRARIES})
  installed_program(pterm)

  add_executable(ptermapp
    uxpterm.c
    gtkapp.c
    ${CMAKE_SOURCE_DIR}/nocmdline.c
    ${CMAKE_SOURCE_DIR}/be_none.c
    ${CMAKE_SOURCE_DIR}/nogss.c
    uxpty.c)
  target_link_libraries(ptermapp
    guiterminal guimisc eventloop settings charset utils
    ${GTK_LIBRARIES} ${X11_LIBRARIES})

  add_executable(putty
    uxputty.c
    gtkmain.c
    ${CMAKE_SOURCE_DIR}/be_all_s.c)
  target_link_libraries(putty
    guiterminal guimisc eventloop sshclient otherbackends settings
    network crypto charset utils
    ${GTK_LIBRARIES} ${X11_LIBRARIES})
  set_target_properties(putty
    PROPERTIES LINK_INTERFACE_MULTIPLICITY 2)
  installed_program(putty)

  add_executable(puttyapp
    uxputty.c
    gtkapp.c
    ${CMAKE_SOURCE_DIR}/nocmdline.c
    ${CMAKE_SOURCE_DIR}/be_all_s.c)
  target_link_libraries(puttyapp
    guiterminal guimisc eventloop sshclient otherbackends settings
    network crypto charset utils
    ${GTK_LIBRARIES} ${X11_LIBRARIES})

  add_executable(puttytel
    uxputty.c
    gtkmain.c
    ${CMAKE_SOURCE_DIR}/be_nos_s.c
    ${CMAKE_SOURCE_DIR}/nogss.c
    ${CMAKE_SOURCE_DIR}/norand.c
    ${CMAKE_SOURCE_DIR}/nocproxy.c)
  target_link_libraries(puttytel
    guiterminal guimisc eventloop otherbackends settings network charset utils
    ${GTK_LIBRARIES} ${X11_LIBRARIES})
endif()
