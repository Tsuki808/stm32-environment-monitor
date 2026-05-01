#ifndef _APP_FUSION_H
#define _APP_FUSION_H

#include "stm32f10x.h"
#include "app_types_pro.h"

// 多源融合评分权重
#define WEIGHT_LIGHT  4
#define WEIGHT_TEMP   2
#define WEIGHT_HUMI   1
#define WEIGHT_GAS    3

// 风险评分阈值
#define RISK_WARN_THRESHOLD   1
#define RISK_ALARM_THRESHOLD  4

// 函数声明
void AppFusion_Init(void);
void AppFusion_Process(EnvRuntime_t *rt, const AppConfig_t *cfg);
uint8_t AppFusion_CalcRiskScore(const EnvRuntime_t *rt, const AppConfig_t *cfg);
void AppFusion_UpdateAlarmSource(EnvRuntime_t *rt, const AppConfig_t *cfg);
uint16_t AppFusion_GetAutoUpgradeTime(const EnvRuntime_t *rt);

#endif
