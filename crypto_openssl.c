// Copyright (c) 2023, Tencent Inc.
// All rights reserved.
#include "wmpf/crypto.h"

#include <assert.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdlib.h>
#include <string.h>

#include "wmpf/macros.h"

static wx_error_t md_update(struct wx_crypto_md_ctx* wx_ctx,
                            const void* data,
                            size_t size);

static wx_error_t md_finish(struct wx_crypto_md_ctx* wx_ctx,
                            void* buffer,
                            size_t* output_len);

static void md_destroy(struct wx_crypto_md_ctx* ctx);

struct wx_crypto_md_ctx base = {
    .common =
        {
            .tag = WX_CRYPTO_MD_CTX_TAG,
            .size = sizeof(struct wx_crypto_md_ctx),
            .version = 0,
        },
    .update = md_update,
    .finish = md_finish,
    .destroy = md_destroy,
};

struct md_ctx {
  struct wx_crypto_md_ctx base;
  EVP_MD_CTX* ctx;
  EVP_PKEY* pkey;
};

const EVP_MD* md_find_md(enum wx_crypto_md_type type) {
  const EVP_MD* md;
  switch (type) {
    case WX_CRYPTO_MD_MD5:
#ifndef OPENSSL_NO_MD5
      md = EVP_md5();
      break;
#else
      return NULL;
#endif
    case WX_CRYPTO_MD_SHA1:
      md = EVP_sha1();
      break;
    case WX_CRYPTO_MD_SHA256:
      md = EVP_sha256();
      break;
    case WX_CRYPTO_MD_SHA512:
      md = EVP_sha512();
      break;
  }

  return md;
}

static struct wx_crypto_md_ctx* md_new(struct wx_crypto_module* module,
                                       enum wx_crypto_md_type type) {
  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->ctx = EVP_MD_CTX_new();
  ctx->pkey = NULL;

  if (!EVP_DigestInit_ex(ctx->ctx, md_find_md(type), NULL)) {
    unsigned long err = ERR_get_error();
    fprintf(stderr, "OpenSSL EVP_DigestInit_ex fail %s",
            ERR_error_string(err, NULL));
    return NULL;
  }

  return &ctx->base;
}

static struct wx_crypto_md_ctx* md_new_pkey(enum wx_crypto_md_type type,
                                            EVP_PKEY* pkey) {
  if (!pkey)
    return NULL;

  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->ctx = EVP_MD_CTX_new();
  ctx->pkey = pkey;

  if (!EVP_DigestSignInit(ctx->ctx, NULL, md_find_md(type), NULL, ctx->pkey)) {
    unsigned long err = ERR_get_error();
    fprintf(stderr, "OpenSSL EVP_DigestSignInit fail %s",
            ERR_error_string(err, NULL));
    free(ctx);
    return NULL;
  }

  return &ctx->base;
}

struct wx_crypto_md_ctx* md_new_hmac(struct wx_crypto_module* module,
                                     enum wx_crypto_md_type type,
                                     const void* key,
                                     size_t key_len) {
  return md_new_pkey(type,
                     EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, key, key_len));
}

static struct wx_crypto_md_ctx* md_new_rsa_sign_private(
    struct wx_crypto_module* module,
    enum wx_crypto_md_type type,
    const char* private_key) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO_write(bio, private_key, strlen(private_key));

  EVP_PKEY* pkey = NULL;
  PEM_read_bio_PrivateKey(bio, &pkey, NULL, NULL);

  BIO_free(bio);

  return md_new_pkey(type, pkey);
}

static struct wx_crypto_md_ctx* md_new_rsa_sign_public(
    struct wx_crypto_module* module,
    enum wx_crypto_md_type type,
    const char* public_key) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO_write(bio, public_key, strlen(public_key));

  EVP_PKEY* pkey = NULL;
  PEM_read_bio_PUBKEY(bio, &pkey, NULL, NULL);

  BIO_free(bio);

  return md_new_pkey(type, pkey);
}

static wx_error_t md_update(struct wx_crypto_md_ctx* wx_ctx,
                            const void* data,
                            size_t size) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  int ret;
  if (ctx->pkey) {
    ret = EVP_DigestSignUpdate(ctx->ctx, data, size);
  } else {
    ret = EVP_DigestUpdate(ctx->ctx, data, size);
  }

  if (ret == 1) {
    return WXERROR_OK;
  } else {
    unsigned long err = ERR_get_error();
    fprintf(stderr, "OpenSSL EVP_Digest(Sign)Update fail %s",
            ERR_error_string(err, NULL));
    return WXERROR_INTERNAL;
  }
}

wx_error_t md_finish(struct wx_crypto_md_ctx* wx_ctx,
                     void* buffer,
                     size_t* output_len) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  int ret;
  size_t w;
  if (ctx->pkey) {
    size_t written;
    ret = EVP_DigestSignFinal(ctx->ctx, buffer, &written);
    w = written;
  } else {
    unsigned int written;
    ret = EVP_DigestFinal_ex(ctx->ctx, buffer, &written);
    w = written;
  }
  if (ret != 1) {
    unsigned long err = ERR_get_error();
    fprintf(stderr, "OpenSSL EVP_Digest(Sign)Final(_ex) fail %s",
            ERR_error_string(err, NULL));
    return WXERROR_INTERNAL;
  }
  if (output_len) {
    *output_len = w;
  }
  return WXERROR_OK;
}

void md_destroy(struct wx_crypto_md_ctx* wx_ctx) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  EVP_MD_CTX_free(ctx->ctx);
  free(ctx);
}

struct wx_crypto_module crypto_module = {
    .common =
        {
            .common =
                {
                    .tag = WX_CRYPTO_MODULE_TAG,
                    .size = sizeof(struct wx_crypto_module),
                    .version = 0,
                },
            .id = WX_CRYPTO_MODULE_ID,
            .set_on_devices_changed = NULL,
        },
    .md_new = md_new,
    .md_new_hmac = md_new_hmac,
    .md_new_rsa_sign_private = md_new_rsa_sign_private,
    .md_new_rsa_sign_public = md_new_rsa_sign_public,
};
