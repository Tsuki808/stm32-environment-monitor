#ifndef _APP_BONUS_H
#define _APP_BONUS_H

#include "stm32f10x.h"

#define APP_DEFAULT_WARN_MV       1200
#define APP_DEFAULT_ALARM_MV      2200
#define APP_DEFAULT_UPLOAD_MS     500
#define APP_DEFAULT_BUZZER_EN     1
#define APP_DEFAULT_FAULT_LIMIT   6
#define APP_DEFAULT_DEMO_MODE     0

#define APP_ERR_NONE              0x00
#define APP_ERR_ADC_ZERO_STUCK    0x01
#define APP_ERR_ADC_FULL_STUCK    0x02
#define APP_ERR_ADC_FROZEN        0x04

typedef struct {
    uint32_t magic;
    uint16_t ver;
    uint16_t len;
    uint16_t warn_mv;
    uint16_t alarm_mv;
    uint16_t upload_ms;
    uint8_t buzzer_enable;
    uint8_t demo_mode;
    uint8_t fault_sample_limit;
    uint8_t reserved[3];
    uint32_t seq;
    uint16_t crc16;
} AppBonusConfig_t;

typedef struct {
    uint16_t alarm_total;
    uint16_t warn_total;
    uint16_t fault_total;
    uint8_t max_level_ever;
    uint16_t light_max;
    uint16_t light_min;
    uint32_t run_time_s;
    uint16_t key_total;
    uint16_t cfg_save_total;
} AppBonusStats_t;

extern AppBonusConfig_t g_app_bonus_cfg;

void AppBonus_Init(void);
void AppBonus_SetDefault(AppBonusConfig_t *cfg);
uint8_t AppBonus_SaveConfig(void);
uint8_t AppBonus_ResetConfig(uint8_t save_to_flash);

uint16_t AppBonus_GetWarnMv(void);
uint16_t AppBonus_GetAlarmMv(void);
uint16_t AppBonus_GetUploadMs(void);
uint8_t AppBonus_GetBuzzerEnable(void);
uint8_t AppBonus_GetDemoMode(void);
uint8_t AppBonus_GetFaultSampleLimit(void);
uint8_t AppBonus_GetErrFlags(void);
uint8_t AppBonus_GetMaxLevel(void);

void AppBonus_OnEnvSample(uint16_t adc_mv, uint8_t state, uint8_t level, uint8_t risk);
void AppBonus_UpdateStateSnapshot(uint8_t state, uint8_t level, uint8_t risk);
void AppBonus_OnEvent(const char *event_msg);
void AppBonus_OnKeyPressed(void);

void AppBonus_PrintStats(void);
void AppBonus_PrintConfig(void);
void AppBonus_PrintLogs(void);
void AppBonus_ClearLogs(void);
void AppBonus_ReportSoftSelfTest(uint16_t adc_mv);

uint8_t AppBonus_SetParam(const char *key, uint16_t value);

#endif
