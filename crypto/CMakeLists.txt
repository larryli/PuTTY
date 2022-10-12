add_sources_from_current_dir(crypto
  aes-common.c
  aes-select.c
  aes-sw.c
  aesgcm-common.c
  aesgcm-select.c
  aesgcm-sw.c
  aesgcm-ref-poly.c
  arcfour.c
  argon2.c
  bcrypt.c
  blake2.c
  blowfish.c
  chacha20-poly1305.c
  crc32.c
  des.c
  diffie-hellman.c
  dsa.c
  ecc-arithmetic.c
  ecc-ssh.c
  hash_simple.c
  hmac.c
  mac.c
  mac_simple.c
  md5.c
  mpint.c
  ntru.c
  openssh-certs.c
  prng.c
  pubkey-pem.c
  pubkey-ppk.c
  pubkey-ssh1.c
  rsa.c
  sha256-common.c
  sha256-select.c
  sha256-sw.c
  sha512-common.c
  sha512-select.c
  sha512-sw.c
  sha3.c
  sha1-common.c
  sha1-select.c
  sha1-sw.c
  xdmauth.c)

include(CheckCSourceCompiles)

function(test_compile_with_flags outvar)
  cmake_parse_arguments(OPT "" ""
    "GNU_FLAGS;MSVC_FLAGS;ADD_SOURCES_IF_SUCCESSFUL;TEST_SOURCE" "${ARGN}")

  # Figure out what flags are applicable to this compiler.
  set(flags)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR
     CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(flags ${OPT_GNU_FLAGS})
  endif()
  if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(flags ${OPT_MSVC_FLAGS})
  endif()

  # See if we can compile the provided test program.
  foreach(i ${flags})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${i}")
  endforeach()
  check_c_source_compiles("${OPT_TEST_SOURCE}" "${outvar}")

  if(${outvar} AND OPT_ADD_SOURCES_IF_SUCCESSFUL)
    # Make an object library that compiles the implementation with the
    # necessary flags, and add the resulting objects to the crypto
    # library.
    set(libname object_lib_${outvar})
    add_library(${libname} OBJECT ${OPT_ADD_SOURCES_IF_SUCCESSFUL})
    target_compile_options(${libname} PRIVATE ${flags})
    target_sources(crypto PRIVATE $<TARGET_OBJECTS:${libname}>)
  endif()

  # Export the output to the caller's scope, so that further tests can
  # be based on it.
  set(${outvar} ${${outvar}} PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------
# Try to enable x86 intrinsics-based crypto implementations.

test_compile_with_flags(HAVE_WMMINTRIN_H
  GNU_FLAGS -msse4.1
  TEST_SOURCE "
    #include <wmmintrin.h>
    #include <smmintrin.h>
    volatile __m128i r, a, b;
    int main(void) { r = _mm_xor_si128(a, b); }")
if(HAVE_WMMINTRIN_H)
  test_compile_with_flags(HAVE_AES_NI
    GNU_FLAGS -msse4.1 -maes
    TEST_SOURCE "
      #include <wmmintrin.h>
      #include <smmintrin.h>
      volatile __m128i r, a, b;
      int main(void) { r = _mm_aesenc_si128(a, b); }"
    ADD_SOURCES_IF_SUCCESSFUL aes-ni aes-ni.c)

  # shaintrin.h doesn't exist on all compilers; sometimes it's folded
  # into the other headers
  test_compile_with_flags(HAVE_SHAINTRIN_H
    GNU_FLAGS -msse4.1 -msha
    TEST_SOURCE "
      #include <wmmintrin.h>
      #include <smmintrin.h>
      #include <immintrin.h>
      #include <shaintrin.h>
      volatile __m128i r, a, b;
      int main(void) { r = _mm_xor_si128(a, b); }")
  if(HAVE_SHAINTRIN_H)
    set(include_shaintrin "#include <shaintrin.h>")
  else()
    set(include_shaintrin "")
  endif()

  test_compile_with_flags(HAVE_SHA_NI
    GNU_FLAGS -msse4.1 -msha
    TEST_SOURCE "
      #include <wmmintrin.h>
      #include <smmintrin.h>
      #include <immintrin.h>
      ${include_shaintrin}
      volatile __m128i r, a, b, c;
      int main(void) { r = _mm_sha256rnds2_epu32(a, b, c); }"
    ADD_SOURCES_IF_SUCCESSFUL sha256-ni.c sha1-ni.c)

  test_compile_with_flags(HAVE_CLMUL
    GNU_FLAGS -msse4.1 -mpclmul
    TEST_SOURCE "
      #include <wmmintrin.h>
      #include <tmmintrin.h>
      volatile __m128i r, a, b;
      int main(void) { r = _mm_clmulepi64_si128(a, b, 5);
                       r = _mm_shuffle_epi8(r, a); }"
    ADD_SOURCES_IF_SUCCESSFUL aesgcm-clmul.c)
endif()

# ----------------------------------------------------------------------
# Try to enable Arm Neon intrinsics-based crypto implementations.

# Start by checking which header file we need. ACLE specifies that it
# ought to be <arm_neon.h>, on both 32- and 64-bit Arm, but Visual
# Studio for some reason renamed the header to <arm64_neon.h> in
# 64-bit, and gives an error if you use the standard name. (However,
# clang-cl does let you use the standard name.)
test_compile_with_flags(HAVE_ARM_NEON_H
  MSVC_FLAGS -D_ARM_USE_NEW_NEON_INTRINSICS
  TEST_SOURCE "
    #include <arm_neon.h>
    volatile uint8x16_t r, a, b;
    int main(void) { r = veorq_u8(a, b); }")
if(HAVE_ARM_NEON_H)
  set(neon ON)
  set(neon_header "arm_neon.h")
else()
  test_compile_with_flags(HAVE_ARM64_NEON_H TEST_SOURCE "
    #include <arm64_neon.h>
    volatile uint8x16_t r, a, b;
    int main(void) { r = veorq_u8(a, b); }")
  if(HAVE_ARM64_NEON_H)
    set(neon ON)
    set(neon_header "arm64_neon.h")
    set(USE_ARM64_NEON_H ON)
  endif()
endif()

if(neon)
  # If we have _some_ NEON header, look for the individual things we
  # can enable with it.

  # The 'crypto' architecture extension includes support for AES,
  # SHA-1, and SHA-256.
  test_compile_with_flags(HAVE_NEON_CRYPTO
    GNU_FLAGS -march=armv8-a+crypto
    MSVC_FLAGS -D_ARM_USE_NEW_NEON_INTRINSICS
    TEST_SOURCE "
      #include <${neon_header}>
      volatile uint8x16_t r, a, b;
      volatile uint32x4_t s, x, y, z;
      int main(void) { r = vaeseq_u8(a, b); s = vsha256hq_u32(x, y, z); }"
    ADD_SOURCES_IF_SUCCESSFUL aes-neon.c sha256-neon.c sha1-neon.c)

  test_compile_with_flags(HAVE_NEON_PMULL
    GNU_FLAGS -march=armv8-a+crypto
    MSVC_FLAGS -D_ARM_USE_NEW_NEON_INTRINSICS
    TEST_SOURCE "
      #include <${neon_header}>
      volatile poly128_t r;
      volatile poly64_t a, b;
      volatile poly64x2_t u, v;
      int main(void) { r = vmull_p64(a, b); r = vmull_high_p64(u, v); }"
    ADD_SOURCES_IF_SUCCESSFUL aesgcm-neon.c)

  test_compile_with_flags(HAVE_NEON_VADDQ_P128
    GNU_FLAGS -march=armv8-a+crypto
    MSVC_FLAGS -D_ARM_USE_NEW_NEON_INTRINSICS
    TEST_SOURCE "
      #include <${neon_header}>
      volatile poly128_t r;
      int main(void) { r = vaddq_p128(r, r); }")

  # The 'sha3' architecture extension, despite the name, includes
  # support for SHA-512 (from the SHA-2 standard) as well as SHA-3
  # proper.
  #
  # Versions of clang up to and including clang 12 support this
  # extension in assembly language, but not the ACLE intrinsics for
  # it. So we check both.
  test_compile_with_flags(HAVE_NEON_SHA512_INTRINSICS
    GNU_FLAGS -march=armv8.2-a+crypto+sha3
    TEST_SOURCE "
      #include <${neon_header}>
      volatile uint64x2_t r, a, b;
      int main(void) { r = vsha512su0q_u64(a, b); }"
    ADD_SOURCES_IF_SUCCESSFUL sha512-neon.c)
  if(HAVE_NEON_SHA512_INTRINSICS)
    set(HAVE_NEON_SHA512 ON)
  else()
    test_compile_with_flags(HAVE_NEON_SHA512_ASM
      GNU_FLAGS -march=armv8.2-a+crypto+sha3
      TEST_SOURCE "
        #include <${neon_header}>
        volatile uint64x2_t r, a;
        int main(void) { __asm__(\"sha512su0 %0.2D,%1.2D\" : \"+w\" (r) : \"w\" (a)); }"
      ADD_SOURCES_IF_SUCCESSFUL sha512-neon.c)
    if(HAVE_NEON_SHA512_ASM)
      set(HAVE_NEON_SHA512 ON)
    endif()
  endif()
endif()

set(HAVE_AES_NI ${HAVE_AES_NI} PARENT_SCOPE)
set(HAVE_SHA_NI ${HAVE_SHA_NI} PARENT_SCOPE)
set(HAVE_SHAINTRIN_H ${HAVE_SHAINTRIN_H} PARENT_SCOPE)
set(HAVE_NEON_CRYPTO ${HAVE_NEON_CRYPTO} PARENT_SCOPE)
set(HAVE_NEON_SHA512 ${HAVE_NEON_SHA512} PARENT_SCOPE)
set(HAVE_NEON_SHA512_INTRINSICS ${HAVE_NEON_SHA512_INTRINSICS} PARENT_SCOPE)
set(USE_ARM64_NEON_H ${USE_ARM64_NEON_H} PARENT_SCOPE)
