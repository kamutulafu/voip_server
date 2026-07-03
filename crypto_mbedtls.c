// Copyright (c) 2023, Tencent Inc.
// All rights reserved.
#include "wmpf/crypto.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "wmpf/macros.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"

static wx_error_t md_update(struct wx_crypto_md_ctx* wx_ctx,
                            const void* data,
                            size_t size);

static wx_error_t md_finish(struct wx_crypto_md_ctx* wx_ctx,
                            void* bufferm,
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

enum md_scene {
  MD_SCENE_HASH,
  MD_SCENE_HMAC,
  MD_SCENE_RSA_SIGN,
};

struct md_ctx {
  struct wx_crypto_md_ctx base;
  struct mbedtls_md_context_t ctx;
  enum md_scene scene;

  const mbedtls_md_info_t* info;
  mbedtls_pk_context pk;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
};

static const mbedtls_md_info_t* md_to_info(enum wx_crypto_md_type type) {
  mbedtls_md_type_t mbedtls_md_type;
  switch (type) {
    case WX_CRYPTO_MD_MD5:
      mbedtls_md_type = MBEDTLS_MD_MD5;
      break;
    case WX_CRYPTO_MD_SHA1:
      mbedtls_md_type = MBEDTLS_MD_SHA1;
      break;
    case WX_CRYPTO_MD_SHA256:
      mbedtls_md_type = MBEDTLS_MD_SHA256;
      break;
    case WX_CRYPTO_MD_SHA512:
      mbedtls_md_type = MBEDTLS_MD_SHA512;
      break;
  }

  return mbedtls_md_info_from_type(mbedtls_md_type);
}

struct wx_crypto_md_ctx* md_new(struct wx_crypto_module* module,
                                enum wx_crypto_md_type type) {
  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->scene = MD_SCENE_HASH;
  ctx->info = md_to_info(type);
  mbedtls_md_init(&ctx->ctx);
  int ret = mbedtls_md_setup(&ctx->ctx, ctx->info, false);
  if (ret != 0)
    goto err;
  ret = mbedtls_md_starts(&ctx->ctx);
  if (ret != 0)
    goto err;
  return &ctx->base;
err:
  mbedtls_md_free(&ctx->ctx);
  free(ctx);
  return NULL;
}

struct wx_crypto_md_ctx* md_new_hmac(struct wx_crypto_module* module,
                                     enum wx_crypto_md_type type,
                                     const void* key,
                                     size_t key_len) {
  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->scene = MD_SCENE_HMAC;
  ctx->info = md_to_info(type);
  mbedtls_md_init(&ctx->ctx);
  int ret = mbedtls_md_setup(&ctx->ctx, ctx->info, true);
  if (ret != 0)
    goto err;
  ret = mbedtls_md_hmac_starts(&ctx->ctx, key, key_len);
  if (ret != 0)
    goto err;
  return &ctx->base;
err:
  mbedtls_md_free(&ctx->ctx);
  free(ctx);
  return NULL;
}

struct wx_crypto_md_ctx* md_new_rsa_sign_private(
    struct wx_crypto_module* module,
    enum wx_crypto_md_type type,
    const char* private_key) {
  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->scene = MD_SCENE_RSA_SIGN;
  ctx->info = md_to_info(type);

  mbedtls_entropy_init(&ctx->entropy);
  mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
  const char* pers = "rsa_sign";
  int ret;
  ret =
      mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                            (const unsigned char*)pers, strlen(pers));
  if (ret != 0)
    goto err_ctr_drbg;

  mbedtls_pk_init(&ctx->pk);
  ret = mbedtls_pk_parse_key(&ctx->pk, (const unsigned char*)private_key,
                             strlen(private_key) + 1, NULL, 0,
                             mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
  if (ret != 0)
    goto err_pk;

  if (!mbedtls_pk_can_do(&ctx->pk, MBEDTLS_PK_RSA))
    goto err_pk;

  mbedtls_md_init(&ctx->ctx);
  ret = mbedtls_md_setup(&ctx->ctx, ctx->info, false);
  if (ret != 0)
    goto err_ctx;

  ret = mbedtls_md_starts(&ctx->ctx);
  if (ret != 0)
    goto err_ctx;

  return &ctx->base;

err_ctx:
  mbedtls_md_free(&ctx->ctx);
err_pk:
  mbedtls_pk_free(&ctx->pk);
err_ctr_drbg:
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
  free(ctx);
  return NULL;
}

struct wx_crypto_md_ctx* md_new_rsa_sign_public(struct wx_crypto_module* module,
                                                enum wx_crypto_md_type type,
                                                const char* public_key) {
  struct md_ctx* ctx = malloc(sizeof(struct md_ctx));
  ctx->base = base;
  ctx->scene = MD_SCENE_RSA_SIGN;
  ctx->info = md_to_info(type);

  mbedtls_entropy_init(&ctx->entropy);
  mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
  const char* pers = "rsa_sign";
  int ret;
  ret =
      mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                            (const unsigned char*)pers, strlen(pers));
  if (ret != 0)
    goto err_ctr_drbg;

  mbedtls_pk_init(&ctx->pk);
  ret = mbedtls_pk_parse_public_key(&ctx->pk, (const unsigned char*)public_key,
                                    strlen(public_key) + 1);
  if (ret != 0)
    goto err_pk;

  if (!mbedtls_pk_can_do(&ctx->pk, MBEDTLS_PK_RSA))
    goto err_pk;

  mbedtls_md_init(&ctx->ctx);
  ret = mbedtls_md_setup(&ctx->ctx, ctx->info, false);
  if (ret != 0)
    goto err_ctx;

  ret = mbedtls_md_starts(&ctx->ctx);
  if (ret != 0)
    goto err_ctx;

  return &ctx->base;

err_ctx:
  mbedtls_md_free(&ctx->ctx);
err_pk:
  mbedtls_pk_free(&ctx->pk);
err_ctr_drbg:
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
  free(ctx);
  return NULL;
}

wx_error_t md_update(struct wx_crypto_md_ctx* wx_ctx,
                     const void* data,
                     size_t size) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  int ret;
  if (ctx->scene == MD_SCENE_HMAC) {
    ret = mbedtls_md_hmac_update(&ctx->ctx, data, size);
  } else {
    ret = mbedtls_md_update(&ctx->ctx, data, size);
  }
  if (ret != 0) {
    return WXERROR_INTERNAL;
  } else {
    return WXERROR_OK;
  }
}

wx_error_t md_finish(struct wx_crypto_md_ctx* wx_ctx,
                     void* output,
                     size_t* output_len) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  switch (ctx->scene) {
    case MD_SCENE_HASH: {
      int ret = mbedtls_md_finish(&ctx->ctx, output);
      if (ret != 0)
        return WXERROR_INTERNAL;
      if (output_len)
        *output_len = mbedtls_md_get_size(ctx->info);
      return WXERROR_OK;
    }
    case MD_SCENE_HMAC: {
      int ret = mbedtls_md_hmac_finish(&ctx->ctx, output);
      if (ret != 0)
        return ret;
      if (output_len)
        *output_len = mbedtls_md_get_size(ctx->info);
      return WXERROR_OK;
    }
    case MD_SCENE_RSA_SIGN: {
      uint8_t md[64];
      int ret = mbedtls_md_finish(&ctx->ctx, md);
      if (ret != 0)
        return WXERROR_INTERNAL;

      size_t sig_len = 0;
      ret = mbedtls_pk_sign(&ctx->pk, mbedtls_md_get_type(ctx->info), md,
                            mbedtls_md_get_size(ctx->info), output, 1024,
                            &sig_len, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
      if (ret != 0)
        return WXERROR_INTERNAL;

      if (output_len)
        *output_len = sig_len;
      return WXERROR_OK;
    }
  }
}

void md_destroy(struct wx_crypto_md_ctx* wx_ctx) {
  struct md_ctx* ctx = wx_container_of(wx_ctx, struct md_ctx, base);
  if (ctx->scene == MD_SCENE_RSA_SIGN) {
    mbedtls_pk_free(&ctx->pk);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  }
  mbedtls_md_free(&ctx->ctx);
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
