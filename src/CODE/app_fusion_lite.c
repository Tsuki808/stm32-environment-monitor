#include "app_fusion_lite.h"
#include "protocol.h"

// 融合运行时数据（全局静态）
static FusionRuntime_t g_fusion_rt = {
    .state = STATE_NORMAL,
    .prev_state = STATE_NORMAL,
    .risk_score = 0,
    .stable_count = 0,
    .escalate_count = 0
};

/**
 * @brief 初始化融合模块
 */
void Fusion_Init(void)
{
    g_fusion_rt.state = STATE_NORMAL;
    g_fusion_rt.prev_state = STATE_NORMAL;
    g_fusion_rt.risk_score = 0;
    g_fusion_rt.stable_count = 0;
    g_fusion_rt.escalate_count = 0;
}

/**
 * @brief 计算风险分数（基于单一 ADC 值）
 * @param adc_mv ADC 电压值（mV）
 * @param threshold_warn 警告阈值（mV）
 * @param threshold_alarm 报警阈值（mV）
 * @return 风险分数（0-10）
 */
static uint8_t Fusion_CalcRiskScore(uint16_t adc_mv, uint16_t threshold_warn, uint16_t threshold_alarm)
{
    if (adc_mv < threshold_warn) {
        return 0;  // 正常范围
    } else if (adc_mv < threshold_alarm) {
        // 警告范围：线性映射到 1-3 分
        uint16_t delta = adc_mv - threshold_warn;
        uint16_t range = threshold_alarm - threshold_warn;
        return 1 + (delta * 2 / range);  // 1-3 分
    } else {
        // 报警范围：线性映射到 4-10 分
        uint16_t delta = adc_mv - threshold_alarm;
        uint16_t range = 3300 - threshold_alarm;  // 假设最大 3.3V
        uint8_t score = 4 + (delta * 6 / range);  // 4-10 分
        return (score > 10) ? 10 : score;
    }
}

/**
 * @brief 处理融合逻辑（状态机）
 * @param adc_mv ADC 电压值（mV）
 * @param threshold_warn 警告阈值（mV）
 * @param threshold_alarm 报警阈值（mV）
 * @note 每 500ms 调用一次
 */
void Fusion_Process(uint16_t adc_mv, uint16_t threshold_warn, uint16_t threshold_alarm)
{
    // 保存前一状态
    g_fusion_rt.prev_state = g_fusion_rt.state;

    // 计算风险分数
    g_fusion_rt.risk_score = Fusion_CalcRiskScore(adc_mv, threshold_warn, threshold_alarm);

    // 状态机处理
    switch (g_fusion_rt.state) {
        case STATE_NORMAL:
            if (g_fusion_rt.risk_score >= 4) {
                // 进入一级报警
                g_fusion_rt.state = STATE_ALARM_L1;
                g_fusion_rt.escalate_count = 0;
                Protocol_SendEventFrame("ALARM_ENTER_L1");
            } else if (g_fusion_rt.risk_score >= 1) {
                // 进入警告状态
                g_fusion_rt.state = STATE_WARN;
                Protocol_SendEventFrame("WARN_ENTER");
            }
            g_fusion_rt.stable_count = 0;
            break;

        case STATE_WARN:
            if (g_fusion_rt.risk_score == 0) {
                // 风险消除，开始稳定计数
                g_fusion_rt.stable_count++;
                if (g_fusion_rt.stable_count >= 5) {  // 5 * 500ms = 2.5s
                    // 恢复正常
                    g_fusion_rt.state = STATE_NORMAL;
                    g_fusion_rt.stable_count = 0;
                    Protocol_SendEventFrame("WARN_EXIT");
                }
            } else if (g_fusion_rt.risk_score >= 4) {
                // 升级到一级报警
                g_fusion_rt.state = STATE_ALARM_L1;
                g_fusion_rt.escalate_count = 0;
                g_fusion_rt.stable_count = 0;
                Protocol_SendEventFrame("ALARM_ENTER_L1");
            } else {
                // 仍在警告范围，重置稳定计数
                g_fusion_rt.stable_count = 0;
            }
            break;

        case STATE_ALARM_L1:
            if (g_fusion_rt.risk_score == 0) {
                // 风险消除，开始恢复流程
                g_fusion_rt.stable_count++;
                if (g_fusion_rt.stable_count >= 5) {
                    g_fusion_rt.state = STATE_WARN;
                    g_fusion_rt.stable_count = 0;
                    Protocol_SendEventFrame("ALARM_L1_EXIT");
                }
                g_fusion_rt.escalate_count = 0;
            } else {
                // 风险持续，自动升级计数
                g_fusion_rt.stable_count = 0;
                g_fusion_rt.escalate_count++;
                if (g_fusion_rt.escalate_count >= 4) {  // 4 * 500ms = 2s
                    g_fusion_rt.state = STATE_ALARM_L2;
                    g_fusion_rt.escalate_count = 0;
                    Protocol_SendEventFrame("ALARM_ESCALATE_L2");
                }
            }
            break;

        case STATE_ALARM_L2:
            if (g_fusion_rt.risk_score == 0) {
                // 风险消除，降级到 L1
                g_fusion_rt.stable_count++;
                if (g_fusion_rt.stable_count >= 3) {
                    g_fusion_rt.state = STATE_ALARM_L1;
                    g_fusion_rt.stable_count = 0;
                    Protocol_SendEventFrame("ALARM_L2_EXIT");
                }
                g_fusion_rt.escalate_count = 0;
            } else {
                // 风险持续，继续升级
                g_fusion_rt.stable_count = 0;
                g_fusion_rt.escalate_count++;
                if (g_fusion_rt.escalate_count >= 4) {  // 2s
                    g_fusion_rt.state = STATE_ALARM_L3;
                    g_fusion_rt.escalate_count = 0;
                    Protocol_SendEventFrame("ALARM_ESCALATE_L3");
                }
            }
            break;

        case STATE_ALARM_L3:
            // 最高级别报警，只能通过风险消除降级
            if (g_fusion_rt.risk_score == 0) {
                g_fusion_rt.stable_count++;
                if (g_fusion_rt.stable_count >= 5) {
                    g_fusion_rt.state = STATE_ALARM_L2;
                    g_fusion_rt.stable_count = 0;
                    Protocol_SendEventFrame("ALARM_L3_EXIT");
                }
            } else {
                g_fusion_rt.stable_count = 0;
            }
            break;

        case STATE_FAULT:
            // 故障状态，需要手动复位
            // TODO: 添加故障恢复逻辑
            break;

        default:
            // 异常状态，复位到正常
            g_fusion_rt.state = STATE_NORMAL;
            g_fusion_rt.stable_count = 0;
            g_fusion_rt.escalate_count = 0;
            break;
    }
}

/**
 * @brief 获取当前状态
 */
AlarmState_t Fusion_GetState(void)
{
    return g_fusion_rt.state;
}

/**
 * @brief 获取风险分数
 */
uint8_t Fusion_GetRiskScore(void)
{
    return g_fusion_rt.risk_score;
}

/**
 * @brief 获取标准报警等级
 */
uint8_t Fusion_GetAlarmLevel(void)
{
    switch (g_fusion_rt.state) {
        case STATE_ALARM_L1: return 1;
        case STATE_ALARM_L2: return 2;
        case STATE_ALARM_L3: return 3;
        case STATE_FAULT:    return 3;
        default:             return 0;
    }
}

/**
 * @brief 强制同步融合状态，供 Demo 模式或外部恢复使用
 */
void Fusion_ForceState(AlarmState_t state, uint8_t risk_score)
{
    g_fusion_rt.prev_state = g_fusion_rt.state;
    g_fusion_rt.state = state;
    g_fusion_rt.risk_score = risk_score;
    g_fusion_rt.escalate_count = 0;
    g_fusion_rt.stable_count = 0;
}

/**
 * @brief 检查状态是否发生变化
 * @return 1=状态已变化，0=状态未变化
 */
uint8_t Fusion_IsStateChanged(void)
{
    return (g_fusion_rt.state != g_fusion_rt.prev_state) ? 1 : 0;
}
