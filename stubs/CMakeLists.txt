# This subdirectory is generally full of 'stubs' in the sense of
# functions and types that don't do anything interesting, and are
# substituted in some contexts for ones that do.
#
# Some of the files here, with names beginning 'no-', are substituted
# at link time, conditional on the application. For example, a program
# that doesn't use the timing subsystem but still includes a module
# that makes a passing reference to it (say, in a context that never
# turns out to be called) can link against no-timing.c in place of the
# real timing.c.
#
# Other files, with names beginning 'null-', provide non-functional
# implementations of a particular internal API, or a selection of
# non-functional methods for that API that real implementations can
# selectively use. Those are linked in to a program _alongside_ real
# implementations of the same API.
#
# So the cmake setup for this directory puts all the 'null-' files
# into the utils library (at the end of the link, where they'll be
# available everywhere), but doesn't mention the 'no-' files, because
# those will be selected manually by add_executable() commands
# elsewhere.

add_sources_from_current_dir(utils
  null-lp.c
  null-cipher.c
  null-key.c
  null-mac.c
  null-opener.c
  null-plug.c
  null-seat.c
  null-socket.c)
