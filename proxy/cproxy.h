/*
 * Header for the interaction between proxy.c and cproxy.c. Separated
 * from proxy.h proper so that testcrypt can include it conveniently.
 */

extern const bool socks5_chap_available;
strbuf *chap_response(ptrlen challenge, ptrlen password);
extern const bool http_digest_available;

#define HTTP_DIGEST_HASHES(X)                                           \
    X(HTTP_DIGEST_MD5, "MD5", &ssh_md5, 128)                            \
    X(HTTP_DIGEST_SHA256, "SHA-256", &ssh_sha256, 256)                  \
    X(HTTP_DIGEST_SHA512_256, "SHA-512-256", &ssh_sha512, 256)          \
    /* end of list */
typedef enum HttpDigestHash {
    #define DECL_ENUM(id, str, alg, bits) id,
    HTTP_DIGEST_HASHES(DECL_ENUM)
    #undef DECL_ENUM
    N_HTTP_DIGEST_HASHES
} HttpDigestHash;
extern const char *const httphashnames[];
void http_digest_response(BinarySink *bs, ptrlen username, ptrlen password,
                          ptrlen realm, ptrlen method, ptrlen uri, ptrlen qop,
                          ptrlen nonce, ptrlen opaque, uint32_t nonce_count,
                          HttpDigestHash hash, bool hash_username);
