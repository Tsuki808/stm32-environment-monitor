#ifndef _APP_DIAG_H
#define _APP_DIAG_H

#include "stm32f10x.h"
#include "app_types_pro.h"

// IWDG 看门狗初始化 (约3秒超时)
void AppDiag_IWDG_Init(void);

// 喂狗
void AppDiag_IWDG_Feed(void);

// 上电自检流程
// 返回1表示通过，返回0表示硬件故障
uint8_t AppDiag_PowerOnSelfTest(void);
uint8_t AppDiag_WasWatchdogReset(void);

// 传感器冻结检测更新
// 每秒调用一次
void AppDiag_CheckFrozen(EnvRuntime_t *rt);

#endif
