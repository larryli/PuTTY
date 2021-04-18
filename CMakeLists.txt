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
  sshaes.c sshauxcrypt.c sshdes.c sshdss.c sshecc.c sshhmac.c sshmd5.c sshrsa.c
  sshsh256.c sshsh512.c sshsha.c sshsha3.c
  ecc.c mpint.c
  sshprng.c
  sshcrc.c
  sshdh.c sshmac.c
  ssharcf.c sshblowf.c sshccp.c
  sshblake2.c sshargon2.c sshbcrypt.c
  cproxy.c)

add_library(network STATIC
  be_misc.c nullplug.c errsock.c proxy.c logging.c x11disp.c)

add_library(keygen STATIC
  millerrabin.c mpunsafe.c pockle.c primecandidate.c smallprimes.c
  sshdssg.c sshecdsag.c sshprime.c sshrsag.c
  import.c)

add_library(agent STATIC
  sshpubk.c pageant.c aqsync.c)

add_library(guiterminal STATIC
  terminal.c ldisc.c minibidi.c config.c dialog.c
  $<TARGET_OBJECTS:logging>)

add_library(noterminal STATIC
  noterm.c ldisc.c)

add_library(sshcommon OBJECT
  ssh1bpp.c ssh1censor.c
  ssh1connection.c ssh1login.c ssh2bpp-bare.c ssh2bpp.c ssh2censor.c
  ssh2connection.c ssh2transhk.c ssh2transport.c ssh2userauth.c
  sshcommon.c sshcrcda.c sshgssc.c sshpubk.c sshrand.c
  sshverstring.c sshzlib.c
  pgssapi.c portfwd.c x11fwd.c)

add_library(sftpcommon OBJECT
  sftpcommon.c)

add_library(all-backends OBJECT
  pinger.c)

add_library(sshclient STATIC
  ssh1connection-client.c ssh2connection-client.c ssh2kex-client.c
  sshshare.c ssh.c
  mainchan.c agentf.c
  $<TARGET_OBJECTS:sshcommon>
  $<TARGET_OBJECTS:all-backends>
  $<TARGET_OBJECTS:logging>)

add_library(sshserver STATIC
  ssh1connection-server.c ssh1login-server.c ssh2connection-server.c
  ssh2kex-server.c ssh2userauth-server.c sshserver.c
  sesschan.c
  sftpserver.c
  $<TARGET_OBJECTS:sftpcommon>
  $<TARGET_OBJECTS:sshcommon>)

add_library(sftpclient STATIC
  psftpcommon.c sftp.c $<TARGET_OBJECTS:sftpcommon>)

add_library(otherbackends STATIC
  telnet.c rlogin.c raw.c supdup.c
  $<TARGET_OBJECTS:all-backends>
  $<TARGET_OBJECTS:logging>)

add_executable(testcrypt
  testcrypt.c sshpubk.c sshcrcda.c)
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
