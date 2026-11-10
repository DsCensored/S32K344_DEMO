/**
 * @file    hse_aes.h
 * @brief   HSE AES-128（ECB）封装：给 UDS 0x27 的 16 字节密钥用
 *
 *   ECU 侧：key16 = AES128-ECB(HSE密钥[level], seed16)
 *   PC 侧 ：sa_demo.dll 里用同样的算法和同样的两个密钥
 *
 *   两个密钥放在 HSE 的 RAM key catalog（group 0, slot 0/1），
 *   由 HseAes_LoadKeys() 导入；Bootloader 的 0x31 01 AA01 例程调用它。
 */
#ifndef HSE_AES_H
#define HSE_AES_H

#include <stdint.h>

#define HSE_AES_LEVEL1   (1U)   /* 0x27 01/02 -> 密钥 1 */
#define HSE_AES_LEVEL2   (2U)   /* 0x27 03/04 -> 密钥 2 */

/** 0 = 成功 */
uint8_t HseAes_Init(void);

/** 把两个 AES-128 密钥导入 HSE RAM 密钥槽（可重复调用，幂等） */
uint8_t HseAes_LoadKeys(void);

/** 用指定等级对应的密钥做 AES-128-ECB 加密一个 16 字节块 */
uint8_t HseAes_EncryptBlock(uint8_t level, const uint8_t in[16], uint8_t out[16]);

/** 最近一次失败原因（HSE 服务返回码，0 = OK） */
uint8_t HseAes_GetLastError(void);

/** 诊断（调试器可读）：[0]=LoadKeys结果 [1]=ENC结果 [2..5]=最后一次密文前 4 字 */
extern volatile uint32_t g_hseAesDiag[8];

#endif /* HSE_AES_H */
