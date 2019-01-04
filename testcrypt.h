/*
 * mpint.h functions.
 */
FUNC(val_mpint, mp_new, uint)
FUNC(void, mp_clear, val_mpint)
FUNC(val_mpint, mp_from_bytes_le, val_string_ptrlen)
FUNC(val_mpint, mp_from_bytes_be, val_string_ptrlen)
FUNC(val_mpint, mp_from_integer, uint)
FUNC(val_mpint, mp_from_decimal_pl, val_string_ptrlen)
FUNC(val_mpint, mp_from_decimal, val_string_asciz)
FUNC(val_mpint, mp_from_hex_pl, val_string_ptrlen)
FUNC(val_mpint, mp_from_hex, val_string_asciz)
FUNC(val_mpint, mp_copy, val_mpint)
FUNC(val_mpint, mp_power_2, uint)
FUNC(uint, mp_get_byte, val_mpint, uint)
FUNC(uint, mp_get_bit, val_mpint, uint)
FUNC(void, mp_set_bit, val_mpint, uint, uint)
FUNC(uint, mp_max_bytes, val_mpint)
FUNC(uint, mp_max_bits, val_mpint)
FUNC(uint, mp_get_nbits, val_mpint)
FUNC(val_string_asciz, mp_get_decimal, val_mpint)
FUNC(val_string_asciz, mp_get_hex, val_mpint)
FUNC(val_string_asciz, mp_get_hex_uppercase, val_mpint)
FUNC(uint, mp_cmp_hs, val_mpint, val_mpint)
FUNC(uint, mp_cmp_eq, val_mpint, val_mpint)
FUNC(uint, mp_hs_integer, val_mpint, uint)
FUNC(uint, mp_eq_integer, val_mpint, uint)
FUNC(void, mp_min_into, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_min, val_mpint, val_mpint)
FUNC(void, mp_copy_into, val_mpint, val_mpint)
FUNC(void, mp_select_into, val_mpint, val_mpint, val_mpint, uint)
FUNC(void, mp_add_into, val_mpint, val_mpint, val_mpint)
FUNC(void, mp_sub_into, val_mpint, val_mpint, val_mpint)
FUNC(void, mp_mul_into, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_add, val_mpint, val_mpint)
FUNC(val_mpint, mp_sub, val_mpint, val_mpint)
FUNC(val_mpint, mp_mul, val_mpint, val_mpint)
FUNC(void, mp_add_integer_into, val_mpint, val_mpint, uint)
FUNC(void, mp_sub_integer_into, val_mpint, val_mpint, uint)
FUNC(void, mp_mul_integer_into, val_mpint, val_mpint, uint)
FUNC(void, mp_cond_add_into, val_mpint, val_mpint, val_mpint, uint)
FUNC(void, mp_cond_sub_into, val_mpint, val_mpint, val_mpint, uint)
FUNC(void, mp_cond_swap, val_mpint, val_mpint, uint)
FUNC(void, mp_cond_clear, val_mpint, uint)
FUNC(void, mp_divmod_into, val_mpint, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_div, val_mpint, val_mpint)
FUNC(val_mpint, mp_mod, val_mpint, val_mpint)
FUNC(void, mp_reduce_mod_2to, val_mpint, uint)
FUNC(val_mpint, mp_invert_mod_2to, val_mpint, uint)
FUNC(val_mpint, mp_invert, val_mpint, val_mpint)
FUNC(val_modsqrt, modsqrt_new, val_mpint, val_mpint)
/* The modsqrt functions' 'success' pointer becomes a second return value */
FUNC(val_mpint, mp_modsqrt, val_modsqrt, val_mpint, out_uint)
FUNC(val_monty, monty_new, val_mpint)
FUNC(val_mpint, monty_modulus, val_monty)
FUNC(val_mpint, monty_identity, val_monty)
FUNC(void, monty_import_into, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_import, val_monty, val_mpint)
FUNC(void, monty_export_into, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_export, val_monty, val_mpint)
FUNC(void, monty_mul_into, val_monty, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, monty_add, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_sub, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_mul, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_pow, val_monty, val_mpint, val_mpint)
FUNC(val_mpint, monty_invert, val_monty, val_mpint)
FUNC(val_mpint, monty_modsqrt, val_modsqrt, val_mpint, out_uint)
FUNC(val_mpint, mp_modpow, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_modmul, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_modadd, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_modsub, val_mpint, val_mpint, val_mpint)
FUNC(val_mpint, mp_rshift_safe, val_mpint, uint)
FUNC(void, mp_lshift_fixed_into, val_mpint, val_mpint, uint)
FUNC(void, mp_rshift_fixed_into, val_mpint, val_mpint, uint)
FUNC(val_mpint, mp_rshift_fixed, val_mpint, uint)
FUNC(val_mpint, mp_random_bits, uint)
FUNC(val_mpint, mp_random_in_range, val_mpint, val_mpint)

/*
 * ecc.h functions.
 */
FUNC(val_wcurve, ecc_weierstrass_curve, val_mpint, val_mpint, val_mpint, opt_val_mpint)
FUNC(val_wpoint, ecc_weierstrass_point_new_identity, val_wcurve)
FUNC(val_wpoint, ecc_weierstrass_point_new, val_wcurve, val_mpint, val_mpint)
FUNC(val_wpoint, ecc_weierstrass_point_new_from_x, val_wcurve, val_mpint, uint)
FUNC(val_wpoint, ecc_weierstrass_point_copy, val_wpoint)
FUNC(uint, ecc_weierstrass_point_valid, val_wpoint)
FUNC(val_wpoint, ecc_weierstrass_add_general, val_wpoint, val_wpoint)
FUNC(val_wpoint, ecc_weierstrass_add, val_wpoint, val_wpoint)
FUNC(val_wpoint, ecc_weierstrass_double, val_wpoint)
FUNC(val_wpoint, ecc_weierstrass_multiply, val_wpoint, val_mpint)
FUNC(uint, ecc_weierstrass_is_identity, val_wpoint)
/* The output pointers in get_affine all become extra output values */
FUNC(void, ecc_weierstrass_get_affine, val_wpoint, out_val_mpint, out_val_mpint)
FUNC(val_mcurve, ecc_montgomery_curve, val_mpint, val_mpint, val_mpint)
FUNC(val_mpoint, ecc_montgomery_point_new, val_mcurve, val_mpint)
FUNC(val_mpoint, ecc_montgomery_point_copy, val_mpoint)
FUNC(val_mpoint, ecc_montgomery_diff_add, val_mpoint, val_mpoint, val_mpoint)
FUNC(val_mpoint, ecc_montgomery_double, val_mpoint)
FUNC(val_mpoint, ecc_montgomery_multiply, val_mpoint, val_mpint)
FUNC(void, ecc_montgomery_get_affine, val_mpoint, out_val_mpint)
FUNC(val_ecurve, ecc_edwards_curve, val_mpint, val_mpint, val_mpint, opt_val_mpint)
FUNC(val_epoint, ecc_edwards_point_new, val_ecurve, val_mpint, val_mpint)
FUNC(val_epoint, ecc_edwards_point_new_from_y, val_ecurve, val_mpint, uint)
FUNC(val_epoint, ecc_edwards_point_copy, val_epoint)
FUNC(val_epoint, ecc_edwards_add, val_epoint, val_epoint)
FUNC(val_epoint, ecc_edwards_multiply, val_epoint, val_mpint)
FUNC(uint, ecc_edwards_eq, val_epoint, val_epoint)
FUNC(void, ecc_edwards_get_affine, val_epoint, out_val_mpint, out_val_mpint)

/*
 * The ssh_hash abstraction. Note the 'consumed', indicating that
 * ssh_hash_final puts its input ssh_hash beyond use.
 *
 * ssh_hash_update is an invention of testcrypt, handled in the real C
 * API by the hash object also functioning as a BinarySink.
 */
FUNC(val_hash, ssh_hash_new, hashalg)
FUNC(val_hash, ssh_hash_copy, val_hash)
FUNC(val_string, ssh_hash_final, consumed_val_hash)
FUNC(void, ssh_hash_update, val_hash, val_string_ptrlen)

/*
 * The ssh2_mac abstraction. Note the optional ssh2_cipher parameter
 * to ssh2_mac_new. Also, again, I've invented an ssh2_mac_update so
 * you can put data into the MAC.
 */
FUNC(val_mac, ssh2_mac_new, macalg, opt_val_ssh2_cipher)
FUNC(void, ssh2_mac_setkey, val_mac, val_string_ptrlen)
FUNC(void, ssh2_mac_start, val_mac)
FUNC(void, ssh2_mac_update, val_mac, val_string_ptrlen)
FUNC(val_string, ssh2_mac_genresult, val_mac)

/*
 * The ssh_key abstraction. All the uses of BinarySink and
 * BinarySource in parameters are replaced with ordinary strings for
 * the testing API: new_priv_openssh just takes a string input, and
 * all the functions that output key and signature blobs do it by
 * returning a string.
 */
FUNC(val_key, ssh_key_new_pub, keyalg, val_string_ptrlen)
FUNC(val_key, ssh_key_new_priv, keyalg, val_string_ptrlen, val_string_ptrlen)
FUNC(val_key, ssh_key_new_priv_openssh, keyalg, val_string_binarysource)
FUNC(void, ssh_key_sign, val_key, val_string_ptrlen, uint, out_val_string_binarysink)
FUNC(boolean, ssh_key_verify, val_key, val_string_ptrlen, val_string_ptrlen)
FUNC(void, ssh_key_public_blob, val_key, out_val_string_binarysink)
FUNC(void, ssh_key_private_blob, val_key, out_val_string_binarysink)
FUNC(void, ssh_key_openssh_blob, val_key, out_val_string_binarysink)
FUNC(val_string_asciz, ssh_key_cache_str, val_key)
FUNC(uint, ssh_key_public_bits, keyalg, val_string_ptrlen)

/*
 * The ssh1_cipher abstraction. The in-place encrypt and decrypt
 * functions are wrapped to replace them with a pair that take one
 * string and return a separate string.
 */
FUNC(val_ssh1_cipher, ssh1_cipher_new, ssh1_cipheralg)
FUNC(void, ssh1_cipher_sesskey, val_ssh1_cipher, val_string_ptrlen)
FUNC(val_string, ssh1_cipher_encrypt, val_ssh1_cipher, val_string_ptrlen)
FUNC(val_string, ssh1_cipher_decrypt, val_ssh1_cipher, val_string_ptrlen)

/*
 * The ssh2_cipher abstraction, with similar modifications.
 */
FUNC(val_ssh2_cipher, ssh2_cipher_new, ssh2_cipheralg)
FUNC(void, ssh2_cipher_setiv, val_ssh2_cipher, val_string_ptrlen)
FUNC(void, ssh2_cipher_setkey, val_ssh2_cipher, val_string_ptrlen)
FUNC(val_string, ssh2_cipher_encrypt, val_ssh2_cipher, val_string_ptrlen)
FUNC(val_string, ssh2_cipher_decrypt, val_ssh2_cipher, val_string_ptrlen)
FUNC(val_string, ssh2_cipher_encrypt_length, val_ssh2_cipher, val_string_ptrlen, uint)
FUNC(val_string, ssh2_cipher_decrypt_length, val_ssh2_cipher, val_string_ptrlen, uint)

/*
 * Integer Diffie-Hellman.
 */
FUNC(val_dh, dh_setup_group, dh_group)
FUNC(val_dh, dh_setup_gex, val_mpint, val_mpint)
FUNC(uint, dh_modulus_bit_size, val_dh)
FUNC(val_mpint, dh_create_e, val_dh, uint)
FUNC(boolean, dh_validate_f, val_dh, val_mpint)
FUNC(val_mpint, dh_find_K, val_dh, val_mpint)

/*
 * Elliptic-curve Diffie-Hellman.
 */
FUNC(val_ecdh, ssh_ecdhkex_newkey, ecdh_alg)
FUNC(void, ssh_ecdhkex_getpublic, val_ecdh, out_val_string_binarysink)
FUNC(val_mpint, ssh_ecdhkex_getkey, val_ecdh, val_string_ptrlen)

/*
 * RSA key exchange.
 */
FUNC(val_rsakex, ssh_rsakex_newkey, val_string_ptrlen)
FUNC(uint, ssh_rsakex_klen, val_rsakex)
FUNC(val_string, ssh_rsakex_encrypt, val_rsakex, hashalg, val_string_ptrlen)
FUNC(val_mpint, ssh_rsakex_decrypt, val_rsakex, hashalg, val_string_ptrlen)

/*
 * Bare RSA keys as used in SSH-1. The construction API functions
 * write into an existing RSAKey object, so I've invented an 'rsa_new'
 * function to make one in the first place.
 */
FUNC(val_rsa, rsa_new)
FUNC(void, get_rsa_ssh1_pub, val_string_binarysource, val_rsa, rsaorder)
FUNC(void, get_rsa_ssh1_priv, val_string_binarysource, val_rsa)
FUNC(val_string, rsa_ssh1_encrypt, val_string_ptrlen, val_rsa)
FUNC(val_mpint, rsa_ssh1_decrypt, val_mpint, val_rsa)
FUNC(val_string, rsa_ssh1_decrypt_pkcs1, val_mpint, val_rsa)
FUNC(val_string_asciz, rsastr_fmt, val_rsa)
FUNC(val_string_asciz, rsa_ssh1_fingerprint, val_rsa)
FUNC(void, rsa_ssh1_public_blob, out_val_string_binarysink, val_rsa, rsaorder)
FUNC(int, rsa_ssh1_public_blob_len, val_string_ptrlen)

/*
 * Miscellaneous.
 */
FUNC(val_wpoint, ecdsa_public, val_mpint, keyalg)
FUNC(val_epoint, eddsa_public, val_mpint, keyalg)
FUNC(val_string, des_encrypt_xdmauth, val_string_ptrlen, val_string_ptrlen)
FUNC(val_string, des_decrypt_xdmauth, val_string_ptrlen, val_string_ptrlen)

/*
 * These functions aren't part of PuTTY's own API, but are additions
 * by testcrypt itself for administrative purposes.
 */
FUNC(void, random_queue, val_string_ptrlen)
FUNC(uint, random_queue_len)
FUNC(void, random_clear)
