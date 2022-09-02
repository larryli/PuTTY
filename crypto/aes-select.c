/*
 * Top-level vtables to select an AES implementation.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "aes.h"

static ssh_cipher *aes_select(const ssh_cipheralg *alg)
{
    const ssh_cipheralg *const *real_algs = (const ssh_cipheralg **)alg->extra;

    for (size_t i = 0; real_algs[i]; i++) {
        const ssh_cipheralg *alg = real_algs[i];
        const struct aes_extra *alg_extra =
            (const struct aes_extra *)alg->extra;
        if (check_availability(alg_extra))
            return ssh_cipher_new(alg);
    }

    /* We should never reach the NULL at the end of the list, because
     * the last non-NULL entry should be software-only AES, which is
     * always available. */
    unreachable("aes_select ran off the end of its list");
}

#if HAVE_AES_NI
#define IF_NI(...) __VA_ARGS__
#else
#define IF_NI(...)
#endif

#if HAVE_NEON_CRYPTO
#define IF_NEON(...) __VA_ARGS__
#else
#define IF_NEON(...)
#endif

#define AES_SELECTOR_VTABLE(mode_c, namemaker, mode_display, bits, ...) \
    static const ssh_cipheralg *                                        \
    ssh_aes ## bits ## _ ## mode_c ## _impls[] = {                      \
        IF_NI(&ssh_aes ## bits ## _ ## mode_c ## _ni,)                  \
        IF_NEON(&ssh_aes ## bits ## _ ## mode_c ## _neon,)              \
        &ssh_aes ## bits ## _ ## mode_c ## _sw,                         \
        NULL,                                                           \
    };                                                                  \
    const ssh_cipheralg ssh_aes ## bits ## _ ## mode_c = {              \
        .new = aes_select,                                              \
        .ssh2_id = namemaker(bits),                                     \
        .blksize = 16,                                                  \
        .real_keybits = bits,                                           \
        .padded_keybytes = bits/8,                                      \
        .text_name = "AES-" #bits " " mode_display                      \
        " (dummy selector vtable)",                                     \
        .extra = ssh_aes ## bits ## _ ## mode_c ## _impls,              \
        __VA_ARGS__                                                     \
    }

#define cbc_namemaker(bits) "aes" #bits "-cbc"
#define ctr_namemaker(bits) "aes" #bits "-ctr"
#define gcm_namemaker(bits) "aes" #bits "-gcm@openssh.com"

AES_SELECTOR_VTABLE(cbc, cbc_namemaker, "CBC", 128);
AES_SELECTOR_VTABLE(cbc, cbc_namemaker, "CBC", 192);
AES_SELECTOR_VTABLE(cbc, cbc_namemaker, "CBC", 256);
AES_SELECTOR_VTABLE(sdctr, ctr_namemaker, "SDCTR", 128);
AES_SELECTOR_VTABLE(sdctr, ctr_namemaker, "SDCTR", 192);
AES_SELECTOR_VTABLE(sdctr, ctr_namemaker, "SDCTR", 256);
AES_SELECTOR_VTABLE(gcm, gcm_namemaker, "GCM", 128,
                    .required_mac = &ssh2_aesgcm_mac);
AES_SELECTOR_VTABLE(gcm, gcm_namemaker, "GCM", 256,
                    .required_mac = &ssh2_aesgcm_mac);

/* 192-bit AES-GCM is included only so that testcrypt can run standard
 * test vectors against it. OpenSSH doesn't define a protocol id for
 * it. Hence the use of null_namemaker here to set its ssh2_id to NULL,
 * and more importantly, leaving it out of aesgcm_list[] below. */
#define null_namemaker(bits) NULL
AES_SELECTOR_VTABLE(gcm, null_namemaker, "GCM", 192,
                    .required_mac = &ssh2_aesgcm_mac);

static const ssh_cipheralg ssh_rijndael_lysator = {
    /* Same as aes256_cbc, but with a different protocol ID */
    .new = aes_select,
    .ssh2_id = "rijndael-cbc@lysator.liu.se",
    .blksize = 16,
    .real_keybits = 256,
    .padded_keybytes = 256/8,
    .text_name = "AES-256 CBC (dummy selector vtable)",
    .extra = ssh_aes256_cbc_impls,
};

static const ssh_cipheralg *const aes_list[] = {
    &ssh_aes256_sdctr,
    &ssh_aes256_cbc,
    &ssh_rijndael_lysator,
    &ssh_aes192_sdctr,
    &ssh_aes192_cbc,
    &ssh_aes128_sdctr,
    &ssh_aes128_cbc,
};

const ssh2_ciphers ssh2_aes = { lenof(aes_list), aes_list };

static const ssh_cipheralg *const aesgcm_list[] = {
    /* OpenSSH only defines protocol ids for 128- and 256-bit AES-GCM,
     * not 192-bit. */
    &ssh_aes128_gcm,
    &ssh_aes256_gcm,
};

const ssh2_ciphers ssh2_aesgcm = { lenof(aesgcm_list), aesgcm_list };
