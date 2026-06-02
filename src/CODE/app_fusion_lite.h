#ifndef _APP_FUSION_LITE_H
#define _APP_FUSION_LITE_H

#include "stm32f10x.h"

// 报警状态枚举
typedef enum {
    STATE_NORMAL = 0,    // 正常状态
    STATE_WARN,          // 警告状态
    STATE_ALARM_L1,      // 一级报警
    STATE_ALARM_L2,      // 二级报警
    STATE_ALARM_L3,      // 三级报警
    STATE_FAULT          // 故障状态
} AlarmState_t;

// 融合运行时数据结构
typedef struct {
    AlarmState_t state;       // 当前状态
    AlarmState_t prev_state;  // 前一状态（用于检测状态变化）
    uint8_t risk_score;       // 风险分数（0-10）
    uint16_t stable_count;    // 稳定计数器（用于退出报警）
    uint16_t escalate_count;  // 升级计数器（用于自动升级）
} FusionRuntime_t;

// 初始化融合模块
void Fusion_Init(void);

// 处理融合逻辑（每 500ms 调用一次）
void Fusion_Process(uint16_t adc_mv, uint16_t threshold_warn, uint16_t threshold_alarm);

// 获取当前状态
AlarmState_t Fusion_GetState(void);

// 获取风险分数
uint8_t Fusion_GetRiskScore(void);

// 获取标准报警等级：NORMAL/WARN=0, L1/L2/L3=1/2/3, FAULT=3
uint8_t Fusion_GetAlarmLevel(void);

// Demo 或恢复流程强制同步状态
void Fusion_ForceState(AlarmState_t state, uint8_t risk_score);

// 检查状态是否发生变化（用于触发事件）
uint8_t Fusion_IsStateChanged(void);

#endif
