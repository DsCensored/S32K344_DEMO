/* ============================================================
 * aes128.h —— AES-128 单方向对外接口（用宏隔离）
 * ------------------------------------------------------------
 * 编译前必须定义且只能定义以下之一：
 *   -DAES128_ENCRYPT   只暴露 aes128_crypt()
 *   -DAES128_DECRYPT   只暴露 aes128_decrypt()
 * ============================================================ */
#ifndef AES128_H
#define AES128_H

#include <stdint.h>

#define AES128_ENCRYPT

/* ---- 模式宏：必须二选一 ---- */
#if defined(AES128_ENCRYPT) && defined(AES128_DECRYPT)
#  error "AES128_ENCRYPT 与 AES128_DECRYPT 不能同时定义"
#elif !defined(AES128_ENCRYPT) && !defined(AES128_DECRYPT)
#  error "请定义 AES128_ENCRYPT 或 AES128_DECRYPT 之一"
#endif

/* ---- 接口 1：数组长度宏 ---- */
#define AES128_LEN 16

/* ---- 接口 2：密钥数组 ---- */
extern uint8_t aes128_key_bootloader[AES128_LEN];

/* ---- 接口 3：唯一函数（按模式二选一暴露） ---- */
#if defined(AES128_ENCRYPT)
void aes128_crypt  (uint8_t *seed, const uint8_t *key);   /* 用 key 加密 seed，密文原地写回 */
#else
void aes128_decrypt(uint8_t *seed, const uint8_t *key);   /* 用 key 解密 seed，明文原地写回 */
#endif

#endif /* AES128_H */
