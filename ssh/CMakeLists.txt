add_library(sshcommon OBJECT
  bpp1.c
  bpp2.c
  bpp-bare.c
  ca-config.c
  censor1.c
  censor2.c
  common.c
  connection1.c
  connection2.c
  crc-attack-detector.c
  gssc.c
  login1.c
  pgssapi.c
  portfwd.c
  ../sshpubk.c
  ../sshrand.c
  transient-hostkey-cache.c
  transport2.c
  verstring.c
  x11fwd.c
  zlib.c)

add_library(sftpcommon OBJECT sftpcommon.c)

add_library(sshclient STATIC
  agentf.c
  connection1-client.c
  connection2-client.c
  kex2-client.c
  mainchan.c
  sharing.c
  ssh.c
  userauth2-client.c
  $<TARGET_OBJECTS:sshcommon>
  $<TARGET_OBJECTS:all-backends>
  $<TARGET_OBJECTS:logging>)

add_library(sshserver STATIC
  connection1-server.c
  connection2-server.c
  kex2-server.c
  login1-server.c
  server.c
  sesschan.c
  sftpserver.c
  userauth2-server.c
  $<TARGET_OBJECTS:sftpcommon>
  $<TARGET_OBJECTS:sshcommon>)

add_sources_from_current_dir(sftpclient sftp.c)
target_sources(sftpclient PRIVATE $<TARGET_OBJECTS:sftpcommon>)
