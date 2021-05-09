#!/usr/bin/env python3

# Simple client of the testcrypt system that reports the available
# variants of each of the crypto primitives that have hardware-
# accelerated implementations.
#
# It will report the set of primitives compiled in to testcrypt, and
# also report whether each one can be instantiated at run time.

from testcrypt import *

def get_implementations(alg):
    return get_implementations_commasep(alg).decode("ASCII").split(",")

def list_implementations(alg, checkfn):
    print(f"Implementations of {alg}:")
    for impl in get_implementations(alg):
        if impl == alg:
            continue
        if checkfn(impl):
            print(f"  {impl:<32s} available")
        else:
            print(f"  {impl:<32s} compiled in, but unavailable at run time")

def list_cipher_implementations(alg):
    list_implementations(alg, lambda impl: ssh_cipher_new(impl) is not None)

def list_hash_implementations(alg):
    list_implementations(alg, lambda impl: ssh_hash_new(impl) is not None)

list_cipher_implementations("aes256_cbc")
list_hash_implementations("sha1")
list_hash_implementations("sha256")
list_hash_implementations("sha512")
