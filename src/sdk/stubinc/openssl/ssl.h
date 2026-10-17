/* Minimal OpenSSL stubs: only opaque types referenced by clamav.h API signatures.
 * The official ClamAV win.x64 binary package ships no OpenSSL headers, but clamav.h
 * includes <openssl/ssl.h>; these forward declarations are enough to compile. */
#ifndef STUB_OPENSSL_SSL_H
#define STUB_OPENSSL_SSL_H
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct x509_st X509;
typedef struct X509_crl_st X509_CRL;
typedef struct asn1_string_st ASN1_TIME;
typedef struct rsa_st RSA;
#include <openssl/err.h>
#endif
