/*
 * List of functions exported by the 'testcrypt' system to provide a
 * Python API for running unit tests and auxiliary programs.
 *
 * Each function definition in this file has the form
 *
 *   FUNC(return-type, function-name, ...)
 *
 * where '...' in turn a variadic list of argument specifications of
 * the form
 *
 *   ARG(argument-type, argument-name)
 *
 * An empty argument list must be marked by including a
 * pseudo-argument VOID:
 *
 *   FUNC(return-type, function-name, VOID)
 *
 * Type names are always single identifiers, and they have some
 * standard prefixes:
 *
 * 'val_' means that the type refers to something dynamically
 * allocated, so that it has a persistent identity, needs to be freed
 * when finished with (though this is done automatically by the
 * testcrypt.py system via Python's reference counting), and may also
 * be mutable. The argument type in C will be a pointer; in Python the
 * corresponding argument will be an instance of a 'Value' object
 * defined in testcrypt.py.
 *
 * 'opt_val_' is a modification of 'val_' to indicate that the pointer
 * may be NULL. In Python this is translated by accepting (or
 * returning) None as an alternative to a Value.
 *
 * 'out_' on an argument type indicates an additional output
 * parameter. The argument type in C has an extra layer of
 * indirection, e.g. an 'out_val_mpint' is an 'mpint **' instead of an
 * 'mpint *', identifying a pointer variable where the returned
 * pointer value will be written. In the Python API, these arguments
 * do not appear in the argument list of the Python function; instead
 * they cause the return value to become a tuple, with additional
 * types appended. For example, a declaration like
 *
 *    FUNC(val_foo, example, ARG(out_val_bar, bar), ARG(val_baz, baz))
 *
 * would identify a function in C with the following prototype, which
 * returns a 'foo *' directly and a 'bar *' by writing it through the
 * provided 'bar **' pointer argument:
 *
 *    foo *example(bar **extra_output, baz *input);
 *
 * and in Python this would become a function taking one argument of
 * type 'baz' and returning a tuple of the form (foo, bar).
 *
 * 'out_' and 'opt_' can go together, if a function returns a second
 * output value but it may in some cases be NULL.
 *
 * 'consumed_' on an argument type indicates that the C function
 * receiving that argument frees it as a side effect.
 *
 * Any argument type which does not start 'val_' is plain old data
 * with no dynamic allocation requirements. Ordinary C integers are
 * sometimes handled this way (e.g. 'uint'). Other plain-data types
 * are represented in Python as a string that must be one of a
 * recognised set of keywords; in C these variously translate into
 * enumeration types (e.g. argon2flavour, rsaorder) or pointers to
 * const vtables of one kind or another (e.g. keyalg, hashalg,
 * primegenpolicy).
 *
 * If a function definition begins with FUNC_WRAPPED rather than FUNC,
 * it means that the underlying C function has a suffix "_wrapper",
 * e.g. ssh_cipher_setiv_wrapper(). Those wrappers are defined in
 * testcrypt.c itself, and change the API or semantics in a way that
 * makes the function more Python-friendly.
 */

/*
 * mpint.h functions.
 */
FUNC(val_mpint, mp_new, ARG(uint, maxbits))
FUNC(void, mp_clear, ARG(val_mpint, x))
FUNC(val_mpint, mp_from_bytes_le, ARG(val_string_ptrlen, bytes))
FUNC(val_mpint, mp_from_bytes_be, ARG(val_string_ptrlen, bytes))
FUNC(val_mpint, mp_from_integer, ARG(uint, n))
FUNC(val_mpint, mp_from_decimal_pl, ARG(val_string_ptrlen, decimal))
FUNC(val_mpint, mp_from_decimal, ARG(val_string_asciz, decimal))
FUNC(val_mpint, mp_from_hex_pl, ARG(val_string_ptrlen, hex))
FUNC(val_mpint, mp_from_hex, ARG(val_string_asciz, hex))
FUNC(val_mpint, mp_copy, ARG(val_mpint, x))
FUNC(val_mpint, mp_power_2, ARG(uint, power))
FUNC(uint, mp_get_byte, ARG(val_mpint, x), ARG(uint, byte))
FUNC(uint, mp_get_bit, ARG(val_mpint, x), ARG(uint, bit))
FUNC(void, mp_set_bit, ARG(val_mpint, x), ARG(uint, bit), ARG(uint, val))
FUNC(uint, mp_max_bytes, ARG(val_mpint, x))
FUNC(uint, mp_max_bits, ARG(val_mpint, x))
FUNC(uint, mp_get_nbits, ARG(val_mpint, x))
FUNC(val_string_asciz, mp_get_decimal, ARG(val_mpint, x))
FUNC(val_string_asciz, mp_get_hex, ARG(val_mpint, x))
FUNC(val_string_asciz, mp_get_hex_uppercase, ARG(val_mpint, x))
FUNC(uint, mp_cmp_hs, ARG(val_mpint, a), ARG(val_mpint, b))
FUNC(uint, mp_cmp_eq, ARG(val_mpint, a), ARG(val_mpint, b))
FUNC(uint, mp_hs_integer, ARG(val_mpint, x), ARG(uint, n))
FUNC(uint, mp_eq_integer, ARG(val_mpint, x), ARG(uint, n))
FUNC(void, mp_min_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(val_mpint, y))
FUNC(void, mp_max_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(val_mpint, y))
FUNC(val_mpint, mp_min, ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_mpint, mp_max, ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(void, mp_copy_into, ARG(val_mpint, dest), ARG(val_mpint, src))
FUNC(void, mp_select_into, ARG(val_mpint, dest), ARG(val_mpint, src0),
     ARG(val_mpint, src1), ARG(uint, choose_src1))
FUNC(void, mp_add_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_sub_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_mul_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(val_mpint, mp_add, ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_mpint, mp_sub, ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_mpint, mp_mul, ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(void, mp_and_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_or_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_xor_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_bic_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(void, mp_copy_integer_into, ARG(val_mpint, dest), ARG(uint, n))
FUNC(void, mp_add_integer_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(uint, n))
FUNC(void, mp_sub_integer_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(uint, n))
FUNC(void, mp_mul_integer_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(uint, n))
FUNC(void, mp_cond_add_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b), ARG(uint, yes))
FUNC(void, mp_cond_sub_into, ARG(val_mpint, dest), ARG(val_mpint, a),
     ARG(val_mpint, b), ARG(uint, yes))
FUNC(void, mp_cond_swap, ARG(val_mpint, x0), ARG(val_mpint, x1),
     ARG(uint, swap))
FUNC(void, mp_cond_clear, ARG(val_mpint, x), ARG(uint, clear))
FUNC(void, mp_divmod_into, ARG(val_mpint, n), ARG(val_mpint, d),
     ARG(opt_val_mpint, q), ARG(opt_val_mpint, r))
FUNC(val_mpint, mp_div, ARG(val_mpint, n), ARG(val_mpint, d))
FUNC(val_mpint, mp_mod, ARG(val_mpint, x), ARG(val_mpint, modulus))
FUNC(val_mpint, mp_nthroot, ARG(val_mpint, y), ARG(uint, n),
     ARG(opt_val_mpint, remainder))
FUNC(void, mp_reduce_mod_2to, ARG(val_mpint, x), ARG(uint, p))
FUNC(val_mpint, mp_invert_mod_2to, ARG(val_mpint, x), ARG(uint, p))
FUNC(val_mpint, mp_invert, ARG(val_mpint, x), ARG(val_mpint, modulus))
FUNC(void, mp_gcd_into, ARG(val_mpint, a), ARG(val_mpint, b),
     ARG(opt_val_mpint, gcd_out), ARG(opt_val_mpint, A_out),
     ARG(opt_val_mpint, B_out))
FUNC(val_mpint, mp_gcd, ARG(val_mpint, a), ARG(val_mpint, b))
FUNC(uint, mp_coprime, ARG(val_mpint, a), ARG(val_mpint, b))
FUNC(val_modsqrt, modsqrt_new, ARG(val_mpint, p),
     ARG(val_mpint, any_nonsquare_mod_p))
/* The modsqrt functions' 'success' pointer becomes a second return value */
FUNC(val_mpint, mp_modsqrt, ARG(val_modsqrt, sc), ARG(val_mpint, x),
     ARG(out_uint, success))
FUNC(val_monty, monty_new, ARG(val_mpint, modulus))
FUNC_WRAPPED(val_mpint, monty_modulus, ARG(val_monty, mc))
FUNC_WRAPPED(val_mpint, monty_identity, ARG(val_monty, mc))
FUNC(void, monty_import_into, ARG(val_monty, mc), ARG(val_mpint, dest),
     ARG(val_mpint, x))
FUNC(val_mpint, monty_import, ARG(val_monty, mc), ARG(val_mpint, x))
FUNC(void, monty_export_into, ARG(val_monty, mc), ARG(val_mpint, dest),
     ARG(val_mpint, x))
FUNC(val_mpint, monty_export, ARG(val_monty, mc), ARG(val_mpint, x))
FUNC(void, monty_mul_into, ARG(val_monty, mc), ARG(val_mpint, dest),
     ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_mpint, monty_add, ARG(val_monty, mc), ARG(val_mpint, x),
     ARG(val_mpint, y))
FUNC(val_mpint, monty_sub, ARG(val_monty, mc), ARG(val_mpint, x),
     ARG(val_mpint, y))
FUNC(val_mpint, monty_mul, ARG(val_monty, mc), ARG(val_mpint, x),
     ARG(val_mpint, y))
FUNC(val_mpint, monty_pow, ARG(val_monty, mc), ARG(val_mpint, base),
     ARG(val_mpint, exponent))
FUNC(val_mpint, monty_invert, ARG(val_monty, mc), ARG(val_mpint, x))
FUNC(val_mpint, monty_modsqrt, ARG(val_modsqrt, sc), ARG(val_mpint, mx),
     ARG(out_uint, success))
FUNC(val_mpint, mp_modpow, ARG(val_mpint, base), ARG(val_mpint, exponent),
     ARG(val_mpint, modulus))
FUNC(val_mpint, mp_modmul, ARG(val_mpint, x), ARG(val_mpint, y),
     ARG(val_mpint, modulus))
FUNC(val_mpint, mp_modadd, ARG(val_mpint, x), ARG(val_mpint, y),
     ARG(val_mpint, modulus))
FUNC(val_mpint, mp_modsub, ARG(val_mpint, x), ARG(val_mpint, y),
     ARG(val_mpint, modulus))
FUNC(void, mp_lshift_safe_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(uint, shift))
FUNC(void, mp_rshift_safe_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(uint, shift))
FUNC(val_mpint, mp_rshift_safe, ARG(val_mpint, x), ARG(uint, shift))
FUNC(void, mp_lshift_fixed_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(uint, shift))
FUNC(void, mp_rshift_fixed_into, ARG(val_mpint, dest), ARG(val_mpint, x),
     ARG(uint, shift))
FUNC(val_mpint, mp_rshift_fixed, ARG(val_mpint, x), ARG(uint, shift))
FUNC(val_mpint, mp_random_bits, ARG(uint, bits))
FUNC(val_mpint, mp_random_in_range, ARG(val_mpint, lo), ARG(val_mpint, hi))

/*
 * ecc.h functions.
 */
FUNC(val_wcurve, ecc_weierstrass_curve, ARG(val_mpint, p), ARG(val_mpint, a),
     ARG(val_mpint, b), ARG(opt_val_mpint, nonsquare_mod_p))
FUNC(val_wpoint, ecc_weierstrass_point_new_identity, ARG(val_wcurve, curve))
FUNC(val_wpoint, ecc_weierstrass_point_new, ARG(val_wcurve, curve),
     ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_wpoint, ecc_weierstrass_point_new_from_x, ARG(val_wcurve, curve),
     ARG(val_mpint, x), ARG(uint, desired_y_parity))
FUNC(val_wpoint, ecc_weierstrass_point_copy, ARG(val_wpoint, orig))
FUNC(uint, ecc_weierstrass_point_valid, ARG(val_wpoint, P))
FUNC(val_wpoint, ecc_weierstrass_add_general, ARG(val_wpoint, P),
     ARG(val_wpoint, Q))
FUNC(val_wpoint, ecc_weierstrass_add, ARG(val_wpoint, P), ARG(val_wpoint, Q))
FUNC(val_wpoint, ecc_weierstrass_double, ARG(val_wpoint, P))
FUNC(val_wpoint, ecc_weierstrass_multiply, ARG(val_wpoint, B),
     ARG(val_mpint, n))
FUNC(uint, ecc_weierstrass_is_identity, ARG(val_wpoint, P))
/* The output pointers in get_affine all become extra output values */
FUNC(void, ecc_weierstrass_get_affine, ARG(val_wpoint, P),
     ARG(out_val_mpint, x), ARG(out_val_mpint, y))
FUNC(val_mcurve, ecc_montgomery_curve, ARG(val_mpint, p), ARG(val_mpint, a),
     ARG(val_mpint, b))
FUNC(val_mpoint, ecc_montgomery_point_new, ARG(val_mcurve, curve),
     ARG(val_mpint, x))
FUNC(val_mpoint, ecc_montgomery_point_copy, ARG(val_mpoint, orig))
FUNC(val_mpoint, ecc_montgomery_diff_add, ARG(val_mpoint, P),
     ARG(val_mpoint, Q), ARG(val_mpoint, PminusQ))
FUNC(val_mpoint, ecc_montgomery_double, ARG(val_mpoint, P))
FUNC(val_mpoint, ecc_montgomery_multiply, ARG(val_mpoint, B), ARG(val_mpint, n))
FUNC(void, ecc_montgomery_get_affine, ARG(val_mpoint, P), ARG(out_val_mpint, x))
FUNC(boolean, ecc_montgomery_is_identity, ARG(val_mpoint, P))
FUNC(val_ecurve, ecc_edwards_curve, ARG(val_mpint, p), ARG(val_mpint, d),
     ARG(val_mpint, a), ARG(opt_val_mpint, nonsquare_mod_p))
FUNC(val_epoint, ecc_edwards_point_new, ARG(val_ecurve, curve),
     ARG(val_mpint, x), ARG(val_mpint, y))
FUNC(val_epoint, ecc_edwards_point_new_from_y, ARG(val_ecurve, curve),
     ARG(val_mpint, y), ARG(uint, desired_x_parity))
FUNC(val_epoint, ecc_edwards_point_copy, ARG(val_epoint, orig))
FUNC(val_epoint, ecc_edwards_add, ARG(val_epoint, P), ARG(val_epoint, Q))
FUNC(val_epoint, ecc_edwards_multiply, ARG(val_epoint, B), ARG(val_mpint, n))
FUNC(uint, ecc_edwards_eq, ARG(val_epoint, P), ARG(val_epoint, Q))
FUNC(void, ecc_edwards_get_affine, ARG(val_epoint, P), ARG(out_val_mpint, x),
     ARG(out_val_mpint, y))

/*
 * The ssh_hash abstraction. Note the 'consumed', indicating that
 * ssh_hash_final puts its input ssh_hash beyond use.
 *
 * ssh_hash_update is an invention of testcrypt, handled in the real C
 * API by the hash object also functioning as a BinarySink.
 */
FUNC(opt_val_hash, ssh_hash_new, ARG(hashalg, alg))
FUNC(void, ssh_hash_reset, ARG(val_hash, h))
FUNC(val_hash, ssh_hash_copy, ARG(val_hash, orig))
FUNC_WRAPPED(val_string, ssh_hash_digest, ARG(val_hash, h))
FUNC_WRAPPED(val_string, ssh_hash_final, ARG(consumed_val_hash, h))
FUNC(void, ssh_hash_update, ARG(val_hash, h), ARG(val_string_ptrlen, data))

FUNC(opt_val_hash, blake2b_new_general, ARG(uint, hashlen))

/*
 * The ssh2_mac abstraction. Note the optional ssh_cipher parameter
 * to ssh2_mac_new. Also, again, I've invented an ssh2_mac_update so
 * you can put data into the MAC.
 */
FUNC(val_mac, ssh2_mac_new, ARG(macalg, alg), ARG(opt_val_cipher, cipher))
FUNC(void, ssh2_mac_setkey, ARG(val_mac, m), ARG(val_string_ptrlen, key))
FUNC(void, ssh2_mac_start, ARG(val_mac, m))
FUNC(void, ssh2_mac_update, ARG(val_mac, m), ARG(val_string_ptrlen, data))
FUNC_WRAPPED(val_string, ssh2_mac_genresult, ARG(val_mac, m))
FUNC(val_string_asciz_const, ssh2_mac_text_name, ARG(val_mac, m))

/*
 * The ssh_key abstraction. All the uses of BinarySink and
 * BinarySource in parameters are replaced with ordinary strings for
 * the testing API: new_priv_openssh just takes a string input, and
 * all the functions that output key and signature blobs do it by
 * returning a string.
 */
FUNC(val_key, ssh_key_new_pub, ARG(keyalg, alg), ARG(val_string_ptrlen, pub))
FUNC(opt_val_key, ssh_key_new_priv, ARG(keyalg, alg),
     ARG(val_string_ptrlen, pub), ARG(val_string_ptrlen, priv))
FUNC(opt_val_key, ssh_key_new_priv_openssh, ARG(keyalg, alg),
     ARG(val_string_binarysource, src))
FUNC(opt_val_string_asciz, ssh_key_invalid, ARG(val_key, key), ARG(uint, flags))
FUNC(void, ssh_key_sign, ARG(val_key, key), ARG(val_string_ptrlen, data),
     ARG(uint, flags), ARG(out_val_string_binarysink, sig))
FUNC(boolean, ssh_key_verify, ARG(val_key, key), ARG(val_string_ptrlen, sig),
     ARG(val_string_ptrlen, data))
FUNC(void, ssh_key_public_blob, ARG(val_key, key),
     ARG(out_val_string_binarysink, blob))
FUNC(void, ssh_key_private_blob, ARG(val_key, key),
     ARG(out_val_string_binarysink, blob))
FUNC(void, ssh_key_openssh_blob, ARG(val_key, key),
     ARG(out_val_string_binarysink, blob))
FUNC(val_string_asciz, ssh_key_cache_str, ARG(val_key, key))
FUNC(val_keycomponents, ssh_key_components, ARG(val_key, key))
FUNC(uint, ssh_key_public_bits, ARG(keyalg, self), ARG(val_string_ptrlen, blob))

/*
 * Accessors to retrieve the innards of a 'key_components'.
 */
FUNC(uint, key_components_count, ARG(val_keycomponents, kc))
FUNC(opt_val_string_asciz_const, key_components_nth_name,
     ARG(val_keycomponents, kc), ARG(uint, n))
FUNC(opt_val_string_asciz_const, key_components_nth_str,
     ARG(val_keycomponents, kc), ARG(uint, n))
FUNC(opt_val_mpint, key_components_nth_mp, ARG(val_keycomponents, kc),
     ARG(uint, n))

/*
 * The ssh_cipher abstraction. The in-place encrypt and decrypt
 * functions are wrapped to replace them with versions that take one
 * string and return a separate string.
 */
FUNC(opt_val_cipher, ssh_cipher_new, ARG(cipheralg, alg))
FUNC_WRAPPED(void, ssh_cipher_setiv, ARG(val_cipher, c),
             ARG(val_string_ptrlen, iv))
FUNC_WRAPPED(void, ssh_cipher_setkey, ARG(val_cipher, c),
             ARG(val_string_ptrlen, key))
FUNC_WRAPPED(val_string, ssh_cipher_encrypt, ARG(val_cipher, c),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, ssh_cipher_decrypt, ARG(val_cipher, c),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, ssh_cipher_encrypt_length, ARG(val_cipher, c),
             ARG(val_string_ptrlen, blk), ARG(uint, seq))
FUNC_WRAPPED(val_string, ssh_cipher_decrypt_length, ARG(val_cipher, c),
             ARG(val_string_ptrlen, blk), ARG(uint, seq))

/*
 * Integer Diffie-Hellman.
 */
FUNC(val_dh, dh_setup_group, ARG(dh_group, group))
FUNC(val_dh, dh_setup_gex, ARG(val_mpint, p), ARG(val_mpint, g))
FUNC(uint, dh_modulus_bit_size, ARG(val_dh, ctx))
FUNC(val_mpint, dh_create_e, ARG(val_dh, ctx))
FUNC_WRAPPED(boolean, dh_validate_f, ARG(val_dh, ctx), ARG(val_mpint, f))
FUNC(val_mpint, dh_find_K, ARG(val_dh, ctx), ARG(val_mpint, f))

/*
 * Elliptic-curve Diffie-Hellman.
 */
FUNC(val_ecdh, ssh_ecdhkex_newkey, ARG(ecdh_alg, alg))
FUNC(void, ssh_ecdhkex_getpublic, ARG(val_ecdh, key),
     ARG(out_val_string_binarysink, pub))
FUNC(opt_val_mpint, ssh_ecdhkex_getkey, ARG(val_ecdh, key),
     ARG(val_string_ptrlen, pub))

/*
 * RSA key exchange, and also the BinarySource get function
 * get_ssh1_rsa_priv_agent, which is a convenient way to make an
 * RSAKey for RSA kex testing purposes.
 */
FUNC(val_rsakex, ssh_rsakex_newkey, ARG(val_string_ptrlen, data))
FUNC(uint, ssh_rsakex_klen, ARG(val_rsakex, key))
FUNC(val_string, ssh_rsakex_encrypt, ARG(val_rsakex, key), ARG(hashalg, h),
     ARG(val_string_ptrlen, plaintext))
FUNC(opt_val_mpint, ssh_rsakex_decrypt, ARG(val_rsakex, key), ARG(hashalg, h),
     ARG(val_string_ptrlen, ciphertext))
FUNC(val_rsakex, get_rsa_ssh1_priv_agent, ARG(val_string_binarysource, src))

/*
 * Bare RSA keys as used in SSH-1. The construction API functions
 * write into an existing RSAKey object, so I've invented an 'rsa_new'
 * function to make one in the first place.
 */
FUNC(val_rsa, rsa_new, VOID)
FUNC(void, get_rsa_ssh1_pub, ARG(val_string_binarysource, src),
     ARG(val_rsa, key), ARG(rsaorder, order))
FUNC(void, get_rsa_ssh1_priv, ARG(val_string_binarysource, src),
     ARG(val_rsa, key))
FUNC_WRAPPED(opt_val_string, rsa_ssh1_encrypt, ARG(val_string_ptrlen, data),
             ARG(val_rsa, key))
FUNC(val_mpint, rsa_ssh1_decrypt, ARG(val_mpint, input), ARG(val_rsa, key))
FUNC_WRAPPED(val_string, rsa_ssh1_decrypt_pkcs1, ARG(val_mpint, input),
             ARG(val_rsa, key))
FUNC(val_string_asciz, rsastr_fmt, ARG(val_rsa, key))
FUNC(val_string_asciz, rsa_ssh1_fingerprint, ARG(val_rsa, key))
FUNC(void, rsa_ssh1_public_blob, ARG(out_val_string_binarysink, blob),
     ARG(val_rsa, key), ARG(rsaorder, order))
FUNC(int, rsa_ssh1_public_blob_len, ARG(val_string_ptrlen, data))
FUNC(void, rsa_ssh1_private_blob_agent, ARG(out_val_string_binarysink, blob),
     ARG(val_rsa, key))

/*
 * The PRNG type. Similarly to hashes and MACs, I've invented an extra
 * function prng_seed_update for putting seed data into the PRNG's
 * exposed BinarySink.
 */
FUNC(val_prng, prng_new, ARG(hashalg, hashalg))
FUNC(void, prng_seed_begin, ARG(val_prng, pr))
FUNC(void, prng_seed_update, ARG(val_prng, pr), ARG(val_string_ptrlen, data))
FUNC(void, prng_seed_finish, ARG(val_prng, pr))
FUNC_WRAPPED(val_string, prng_read, ARG(val_prng, pr), ARG(uint, size))
FUNC(void, prng_add_entropy, ARG(val_prng, pr), ARG(uint, source_id),
     ARG(val_string_ptrlen, data))

/*
 * Key load/save functions, or rather, the BinarySource / strbuf API
 * that sits just inside the file I/O versions.
 */
FUNC(boolean, ppk_encrypted_s, ARG(val_string_binarysource, src),
     ARG(out_opt_val_string_asciz, comment))
FUNC(boolean, rsa1_encrypted_s, ARG(val_string_binarysource, src),
     ARG(out_opt_val_string_asciz, comment))
FUNC(boolean, ppk_loadpub_s, ARG(val_string_binarysource, src),
     ARG(out_opt_val_string_asciz, algorithm),
     ARG(out_val_string_binarysink, blob),
     ARG(out_opt_val_string_asciz, comment),
     ARG(out_opt_val_string_asciz_const, error))
FUNC(int, rsa1_loadpub_s, ARG(val_string_binarysource, src),
     ARG(out_val_string_binarysink, blob),
     ARG(out_opt_val_string_asciz, comment),
     ARG(out_opt_val_string_asciz_const, error))
FUNC_WRAPPED(opt_val_key, ppk_load_s, ARG(val_string_binarysource, src),
             ARG(out_opt_val_string_asciz, comment),
             ARG(opt_val_string_asciz, passphrase),
             ARG(out_opt_val_string_asciz_const, error))
FUNC_WRAPPED(int, rsa1_load_s, ARG(val_string_binarysource, src),
             ARG(val_rsa, key), ARG(out_opt_val_string_asciz, comment),
             ARG(opt_val_string_asciz, passphrase),
             ARG(out_opt_val_string_asciz_const, error))
FUNC_WRAPPED(val_string, ppk_save_sb, ARG(val_key, key),
             ARG(opt_val_string_asciz, comment),
             ARG(opt_val_string_asciz, passphrase), ARG(uint, fmt_version),
             ARG(argon2flavour, flavour), ARG(uint, mem), ARG(uint, passes),
             ARG(uint, parallel))
FUNC_WRAPPED(val_string, rsa1_save_sb, ARG(val_rsa, key),
             ARG(opt_val_string_asciz, comment),
             ARG(opt_val_string_asciz, passphrase))

FUNC(val_string_asciz, ssh2_fingerprint_blob, ARG(val_string_ptrlen, blob),
     ARG(fptype, fptype))

/*
 * Password hashing.
 */
FUNC_WRAPPED(val_string, argon2, ARG(argon2flavour, flavour), ARG(uint, mem),
             ARG(uint, passes), ARG(uint, parallel), ARG(uint, taglen),
             ARG(val_string_ptrlen, P), ARG(val_string_ptrlen, S),
             ARG(val_string_ptrlen, K), ARG(val_string_ptrlen, X))
FUNC(val_string, argon2_long_hash, ARG(uint, length),
     ARG(val_string_ptrlen, data))
FUNC_WRAPPED(val_string, openssh_bcrypt, ARG(val_string_ptrlen, passphrase),
             ARG(val_string_ptrlen, salt), ARG(uint, rounds),
             ARG(uint, outbytes))

/*
 * Key generation functions.
 */
FUNC_WRAPPED(val_key, rsa_generate, ARG(uint, bits), ARG(boolean, strong),
             ARG(val_pgc, pgc))
FUNC_WRAPPED(val_key, dsa_generate, ARG(uint, bits), ARG(val_pgc, pgc))
FUNC_WRAPPED(opt_val_key, ecdsa_generate, ARG(uint, bits))
FUNC_WRAPPED(opt_val_key, eddsa_generate, ARG(uint, bits))
FUNC(val_rsa, rsa1_generate, ARG(uint, bits), ARG(boolean, strong),
     ARG(val_pgc, pgc))
FUNC(val_pgc, primegen_new_context, ARG(primegenpolicy, policy))
FUNC_WRAPPED(opt_val_mpint, primegen_generate, ARG(val_pgc, ctx),
             ARG(consumed_val_pcs, pcs))
FUNC(val_string, primegen_mpu_certificate, ARG(val_pgc, ctx), ARG(val_mpint, p))
FUNC(val_pcs, pcs_new, ARG(uint, bits))
FUNC(val_pcs, pcs_new_with_firstbits, ARG(uint, bits), ARG(uint, first),
     ARG(uint, nfirst))
FUNC(void, pcs_require_residue, ARG(val_pcs, s), ARG(val_mpint, mod),
     ARG(val_mpint, res))
FUNC(void, pcs_require_residue_1, ARG(val_pcs, s), ARG(val_mpint, mod))
FUNC(void, pcs_require_residue_1_mod_prime, ARG(val_pcs, s),
     ARG(val_mpint, mod))
FUNC(void, pcs_avoid_residue_small, ARG(val_pcs, s), ARG(uint, mod),
     ARG(uint, res))
FUNC(void, pcs_try_sophie_germain, ARG(val_pcs, s))
FUNC(void, pcs_set_oneshot, ARG(val_pcs, s))
FUNC(void, pcs_ready, ARG(val_pcs, s))
FUNC(void, pcs_inspect, ARG(val_pcs, pcs), ARG(out_val_mpint, limit_out),
     ARG(out_val_mpint, factor_out), ARG(out_val_mpint, addend_out))
FUNC(val_mpint, pcs_generate, ARG(val_pcs, s))
FUNC(val_pockle, pockle_new, VOID)
FUNC(uint, pockle_mark, ARG(val_pockle, pockle))
FUNC(void, pockle_release, ARG(val_pockle, pockle), ARG(uint, mark))
FUNC(pocklestatus, pockle_add_small_prime, ARG(val_pockle, pockle),
     ARG(val_mpint, p))
FUNC_WRAPPED(pocklestatus, pockle_add_prime, ARG(val_pockle, pockle),
             ARG(val_mpint, p), ARG(mpint_list, factors),
             ARG(val_mpint, witness))
FUNC(val_string, pockle_mpu, ARG(val_pockle, pockle), ARG(val_mpint, p))
FUNC(val_millerrabin, miller_rabin_new, ARG(val_mpint, p))
FUNC(mr_result, miller_rabin_test, ARG(val_millerrabin, mr), ARG(val_mpint, w))

/*
 * Miscellaneous.
 */
FUNC(val_wpoint, ecdsa_public, ARG(val_mpint, private_key), ARG(keyalg, alg))
FUNC(val_epoint, eddsa_public, ARG(val_mpint, private_key), ARG(keyalg, alg))
FUNC_WRAPPED(val_string, des_encrypt_xdmauth, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, des_decrypt_xdmauth, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, des3_encrypt_pubkey, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, des3_decrypt_pubkey, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, des3_encrypt_pubkey_ossh, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, iv), ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, des3_decrypt_pubkey_ossh, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, iv), ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, aes256_encrypt_pubkey, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, iv), ARG(val_string_ptrlen, blk))
FUNC_WRAPPED(val_string, aes256_decrypt_pubkey, ARG(val_string_ptrlen, key),
             ARG(val_string_ptrlen, iv), ARG(val_string_ptrlen, blk))
FUNC(uint, crc32_rfc1662, ARG(val_string_ptrlen, data))
FUNC(uint, crc32_ssh1, ARG(val_string_ptrlen, data))
FUNC(uint, crc32_update, ARG(uint, crc_input), ARG(val_string_ptrlen, data))
FUNC(boolean, crcda_detect, ARG(val_string_ptrlen, packet),
     ARG(val_string_ptrlen, iv))
FUNC(val_string, get_implementations_commasep, ARG(val_string_ptrlen, alg))
FUNC(void, http_digest_response, ARG(out_val_string_binarysink, response),
     ARG(val_string_ptrlen, username), ARG(val_string_ptrlen, password),
     ARG(val_string_ptrlen, realm), ARG(val_string_ptrlen, method),
     ARG(val_string_ptrlen, uri), ARG(val_string_ptrlen, qop),
     ARG(val_string_ptrlen, nonce), ARG(val_string_ptrlen, opaque),
     ARG(uint, nonce_count), ARG(httpdigesthash, hash),
     ARG(boolean, hash_username))

/*
 * These functions aren't part of PuTTY's own API, but are additions
 * by testcrypt itself for administrative purposes.
 */
FUNC(void, random_queue, ARG(val_string_ptrlen, data))
FUNC(uint, random_queue_len, VOID)
FUNC(void, random_make_prng, ARG(hashalg, hashalg),
     ARG(val_string_ptrlen, seed))
FUNC(void, random_clear, VOID)
