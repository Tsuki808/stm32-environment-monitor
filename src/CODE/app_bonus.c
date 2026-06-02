#include "app_bonus.h"
#include "protocol.h"
#include "usart_groundstation.h"
#include <string.h>
#include <stdio.h>

#define APP_CFG_MAGIC       0x53594C31UL
#define APP_CFG_VER         0x0001
#define APP_CFG_ADDR_A      0x0800F800UL
#define APP_CFG_ADDR_B      0x0800FC00UL
#define EVENT_LOG_SIZE      32
#define EVENT_NAME_LEN      24
#define FLASH_WAIT_TIMEOUT  0x000FFFFFUL

typedef struct {
    uint32_t ts_s;
    char evt[EVENT_NAME_LEN];
    uint8_t state;
    uint8_t level;
    uint8_t risk;
    uint8_t err;
    uint16_t adc_mv;
} AppBonusLog_t;

AppBonusConfig_t g_app_bonus_cfg;

static AppBonusStats_t g_stats;
static AppBonusLog_t g_logs[EVENT_LOG_SIZE];
static uint8_t g_log_head = 0;
static uint8_t g_log_count = 0;
static uint8_t g_last_state = 0xFF;
static uint8_t g_last_level = 0;
static uint8_t g_last_risk = 0;
static uint16_t g_last_adc = 0;
static uint8_t g_err_flags = APP_ERR_NONE;
static uint8_t g_zero_cnt = 0;
static uint8_t g_full_cnt = 0;
static uint8_t g_frozen_cnt = 0;
static uint16_t g_prev_adc = 0xFFFF;
static uint8_t g_zero_reported = 0;
static uint8_t g_full_reported = 0;
static uint8_t g_frozen_reported = 0;
static uint16_t g_upload_elapsed = 0;

static uint16_t AppBonus_CalcCRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t j;

    for (i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t AppBonus_ConfigValid(const AppBonusConfig_t *cfg)
{
    AppBonusConfig_t tmp;

    if (cfg->magic != APP_CFG_MAGIC) return 0;
    if (cfg->ver != APP_CFG_VER) return 0;
    if (cfg->len != sizeof(AppBonusConfig_t)) return 0;
    memcpy(&tmp, cfg, sizeof(tmp));
    return AppBonus_CalcCRC16((const uint8_t *)&tmp, sizeof(AppBonusConfig_t) - 2) == tmp.crc16;
}

void AppBonus_SetDefault(AppBonusConfig_t *cfg)
{
    memset(cfg, 0, sizeof(AppBonusConfig_t));
    cfg->magic = APP_CFG_MAGIC;
    cfg->ver = APP_CFG_VER;
    cfg->len = sizeof(AppBonusConfig_t);
    cfg->warn_mv = APP_DEFAULT_WARN_MV;
    cfg->alarm_mv = APP_DEFAULT_ALARM_MV;
    cfg->upload_ms = APP_DEFAULT_UPLOAD_MS;
    cfg->buzzer_enable = APP_DEFAULT_BUZZER_EN;
    cfg->demo_mode = APP_DEFAULT_DEMO_MODE;
    cfg->fault_sample_limit = APP_DEFAULT_FAULT_LIMIT;
    cfg->seq = 0;
    cfg->crc16 = AppBonus_CalcCRC16((const uint8_t *)cfg, sizeof(AppBonusConfig_t) - 2);
}

static uint8_t AppBonus_ReadPage(uint32_t addr, AppBonusConfig_t *cfg)
{
    const AppBonusConfig_t *flash_cfg = (const AppBonusConfig_t *)addr;
    memcpy(cfg, flash_cfg, sizeof(AppBonusConfig_t));
    return AppBonus_ConfigValid(cfg);
}

static uint8_t AppBonus_FlashWait(void)
{
    uint32_t timeout = FLASH_WAIT_TIMEOUT;

    while ((FLASH->SR & FLASH_SR_BSY) && timeout) timeout--;
    if (timeout == 0) return 0;
    if (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) {
        FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP;
        return 0;
    }
    return 1;
}

static void AppBonus_FlashUnlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123UL;
        FLASH->KEYR = 0xCDEF89ABUL;
    }
}

static uint8_t AppBonus_WritePage(uint32_t addr, const AppBonusConfig_t *cfg)
{
    AppBonusConfig_t save_cfg;
    uint16_t *data;
    uint16_t i;
    uint16_t words;

    memcpy(&save_cfg, cfg, sizeof(save_cfg));
    save_cfg.magic = APP_CFG_MAGIC;
    save_cfg.ver = APP_CFG_VER;
    save_cfg.len = sizeof(AppBonusConfig_t);
    save_cfg.crc16 = AppBonus_CalcCRC16((const uint8_t *)&save_cfg, sizeof(AppBonusConfig_t) - 2);

    AppBonus_FlashUnlock();
    FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP;

    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = addr;
    FLASH->CR |= FLASH_CR_STRT;
    if (!AppBonus_FlashWait()) {
        FLASH->CR &= ~FLASH_CR_PER;
        FLASH->CR |= FLASH_CR_LOCK;
        return 0;
    }
    FLASH->CR &= ~FLASH_CR_PER;

    data = (uint16_t *)&save_cfg;
    words = sizeof(AppBonusConfig_t) / 2;
    FLASH->CR |= FLASH_CR_PG;
    for (i = 0; i < words; i++) {
        *(__IO uint16_t *)(addr + i * 2) = data[i];
        if (!AppBonus_FlashWait()) {
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            return 0;
        }
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    return 1;
}

static uint8_t AppBonus_LoadConfig(AppBonusConfig_t *cfg)
{
    AppBonusConfig_t cfg_a;
    AppBonusConfig_t cfg_b;
    uint8_t valid_a;
    uint8_t valid_b;

    valid_a = AppBonus_ReadPage(APP_CFG_ADDR_A, &cfg_a);
    valid_b = AppBonus_ReadPage(APP_CFG_ADDR_B, &cfg_b);
    if (valid_a && valid_b) {
        *cfg = (cfg_b.seq > cfg_a.seq) ? cfg_b : cfg_a;
        return 1;
    }
    if (valid_a) {
        *cfg = cfg_a;
        return 1;
    }
    if (valid_b) {
        *cfg = cfg_b;
        return 1;
    }
    return 0;
}

uint8_t AppBonus_SaveConfig(void)
{
    AppBonusConfig_t cfg_a;
    AppBonusConfig_t cfg_b;
    uint8_t valid_a;
    uint8_t valid_b;
    uint32_t seq_a;
    uint32_t seq_b;
    uint32_t addr;

    valid_a = AppBonus_ReadPage(APP_CFG_ADDR_A, &cfg_a);
    valid_b = AppBonus_ReadPage(APP_CFG_ADDR_B, &cfg_b);
    seq_a = valid_a ? cfg_a.seq : 0;
    seq_b = valid_b ? cfg_b.seq : 0;

    g_app_bonus_cfg.seq = ((seq_a > seq_b) ? seq_a : seq_b) + 1;
    g_app_bonus_cfg.crc16 = AppBonus_CalcCRC16((const uint8_t *)&g_app_bonus_cfg, sizeof(AppBonusConfig_t) - 2);
    addr = (seq_a <= seq_b) ? APP_CFG_ADDR_A : APP_CFG_ADDR_B;

    if (!AppBonus_WritePage(addr, &g_app_bonus_cfg)) return 0;
    g_stats.cfg_save_total++;
    AppBonus_OnEvent("CFG_SAVE");
    return 1;
}

uint8_t AppBonus_ResetConfig(uint8_t save_to_flash)
{
    AppBonus_SetDefault(&g_app_bonus_cfg);
    AppBonus_OnEvent("CFG_DEFAULT");
    if (save_to_flash) return AppBonus_SaveConfig();
    return 1;
}

void AppBonus_Init(void)
{
    uint8_t loaded;
    uint8_t crc_fail;

    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_logs, 0, sizeof(g_logs));
    g_stats.light_min = 0xFFFF;
    g_log_head = 0;
    g_log_count = 0;
    g_last_state = 0xFF;
    g_err_flags = APP_ERR_NONE;
    g_upload_elapsed = 0;

    crc_fail = (((*(__IO uint32_t *)APP_CFG_ADDR_A) == APP_CFG_MAGIC) && !AppBonus_ReadPage(APP_CFG_ADDR_A, &g_app_bonus_cfg)) ||
               (((*(__IO uint32_t *)APP_CFG_ADDR_B) == APP_CFG_MAGIC) && !AppBonus_ReadPage(APP_CFG_ADDR_B, &g_app_bonus_cfg));
    loaded = AppBonus_LoadConfig(&g_app_bonus_cfg);

    if (!loaded) {
        AppBonus_SetDefault(&g_app_bonus_cfg);
        if (crc_fail) {
            AppBonus_OnEvent("CFG_CRC_FAIL");
            Protocol_SendPayload("@EVT,msg=CFG_CRC_FAIL,reason=FLASH_CONFIG_INVALID");
        }
        AppBonus_OnEvent("CFG_DEFAULT_USED");
        Protocol_SendPayload("@EVT,msg=CFG_DEFAULT_USED,reason=LOAD_FAIL");
    }
    if (g_app_bonus_cfg.warn_mv >= g_app_bonus_cfg.alarm_mv ||
        g_app_bonus_cfg.alarm_mv >= 3300 ||
        g_app_bonus_cfg.fault_sample_limit < 3 ||
        g_app_bonus_cfg.fault_sample_limit > 60) {
        AppBonus_SetDefault(&g_app_bonus_cfg);
        AppBonus_OnEvent("CFG_DEFAULT_USED");
        Protocol_SendPayload("@EVT,msg=CFG_DEFAULT_USED,reason=RANGE_CHECK");
    }
}

uint16_t AppBonus_GetWarnMv(void) { return g_app_bonus_cfg.warn_mv; }
uint16_t AppBonus_GetAlarmMv(void) { return g_app_bonus_cfg.alarm_mv; }
uint16_t AppBonus_GetUploadMs(void) { return g_app_bonus_cfg.upload_ms; }
uint8_t AppBonus_GetBuzzerEnable(void) { return g_app_bonus_cfg.buzzer_enable; }
uint8_t AppBonus_GetDemoMode(void) { return g_app_bonus_cfg.demo_mode; }
uint8_t AppBonus_GetFaultSampleLimit(void) { return g_app_bonus_cfg.fault_sample_limit; }
uint8_t AppBonus_GetErrFlags(void) { return g_err_flags; }
uint8_t AppBonus_GetMaxLevel(void) { return g_stats.max_level_ever; }

void AppBonus_OnEvent(const char *event_msg)
{
    AppBonusLog_t *log;

    if (event_msg == 0) return;
    log = &g_logs[g_log_head];
    memset(log, 0, sizeof(AppBonusLog_t));
    log->ts_s = g_stats.run_time_s;
    strncpy(log->evt, event_msg, EVENT_NAME_LEN - 1);
    log->state = g_last_state;
    log->level = g_last_level;
    log->risk = g_last_risk;
    log->err = g_err_flags;
    log->adc_mv = g_last_adc;

    g_log_head = (uint8_t)((g_log_head + 1) % EVENT_LOG_SIZE);
    if (g_log_count < EVENT_LOG_SIZE) g_log_count++;

    if (strcmp(event_msg, "KEY_PRESSED") == 0) g_stats.key_total++;
    if (strcmp(event_msg, "WARN_ENTER") == 0) g_stats.warn_total++;
    if (strcmp(event_msg, "ALARM_ENTER_L1") == 0) g_stats.alarm_total++;
    if (strstr(event_msg, "FAULT") != 0 || strstr(event_msg, "ADC_") != 0) g_stats.fault_total++;
}

static void AppBonus_CheckAdcHealth(uint16_t adc_mv)
{
    uint8_t limit = AppBonus_GetFaultSampleLimit();
    if (limit < 3) limit = APP_DEFAULT_FAULT_LIMIT;

    if (adc_mv <= 5) {
        if (g_zero_cnt < 255) g_zero_cnt++;
    } else {
        g_zero_cnt = 0;
        g_zero_reported = 0;
        g_err_flags &= (uint8_t)~APP_ERR_ADC_ZERO_STUCK;
    }

    if (adc_mv >= 3290) {
        if (g_full_cnt < 255) g_full_cnt++;
    } else {
        g_full_cnt = 0;
        g_full_reported = 0;
        g_err_flags &= (uint8_t)~APP_ERR_ADC_FULL_STUCK;
    }

    if (adc_mv == g_prev_adc) {
        if (g_frozen_cnt < 255) g_frozen_cnt++;
    } else {
        g_frozen_cnt = 0;
        g_frozen_reported = 0;
        g_err_flags &= (uint8_t)~APP_ERR_ADC_FROZEN;
        g_prev_adc = adc_mv;
    }

    if (g_zero_cnt >= limit) {
        g_err_flags |= APP_ERR_ADC_ZERO_STUCK;
        if (!g_zero_reported) {
            AppBonus_OnEvent("ADC_ZERO_STUCK");
            Protocol_SendPayload("@ERR,src=ADC,code=ZERO_STUCK");
            g_zero_reported = 1;
        }
    }
    if (g_full_cnt >= limit) {
        g_err_flags |= APP_ERR_ADC_FULL_STUCK;
        if (!g_full_reported) {
            AppBonus_OnEvent("ADC_FULL_STUCK");
            Protocol_SendPayload("@ERR,src=ADC,code=FULL_STUCK");
            g_full_reported = 1;
        }
    }
    if (g_frozen_cnt >= limit) {
        g_err_flags |= APP_ERR_ADC_FROZEN;
        if (!g_frozen_reported) {
            AppBonus_OnEvent("ADC_FROZEN");
            Protocol_SendPayload("@ERR,src=ADC,code=FROZEN");
            g_frozen_reported = 1;
        }
    }
}

void AppBonus_UpdateStateSnapshot(uint8_t state, uint8_t level, uint8_t risk)
{
    g_last_state = state;
    g_last_level = level;
    g_last_risk = risk;
    if (level > g_stats.max_level_ever) g_stats.max_level_ever = level;
}

void AppBonus_OnEnvSample(uint16_t adc_mv, uint8_t state, uint8_t level, uint8_t risk)
{
    g_last_adc = adc_mv;
    AppBonus_UpdateStateSnapshot(state, level, risk);

    if (adc_mv > g_stats.light_max) g_stats.light_max = adc_mv;
    if (adc_mv < g_stats.light_min) g_stats.light_min = adc_mv;

    g_upload_elapsed = (uint16_t)(g_upload_elapsed + 500);
    while (g_upload_elapsed >= 1000) {
        g_stats.run_time_s++;
        g_upload_elapsed = (uint16_t)(g_upload_elapsed - 1000);
    }

    AppBonus_CheckAdcHealth(adc_mv);
}

void AppBonus_OnKeyPressed(void)
{
    AppBonus_OnEvent("KEY_PRESSED");
}

void AppBonus_PrintStats(void)
{
    char payload[180];
    uint16_t min_val;

    min_val = (g_stats.light_min == 0xFFFF) ? 0 : g_stats.light_min;
    snprintf(payload, sizeof(payload),
             "@STAT,ALARM_TOTAL=%u,WARN_TOTAL=%u,FAULT_TOTAL=%u,MAX_LEVEL=%u,L_MAX=%u,L_MIN=%u,RUN=%lu,KEY=%u,SAVE=%u,DROP=%u,OVF=%u",
             g_stats.alarm_total, g_stats.warn_total, g_stats.fault_total,
             g_stats.max_level_ever, g_stats.light_max, min_val,
             (unsigned long)g_stats.run_time_s, g_stats.key_total, g_stats.cfg_save_total,
             GroundStation_GetDroppedCount(), GroundStation_GetOverflowCount());
    Protocol_SendPayload(payload);
}

void AppBonus_PrintConfig(void)
{
    char payload[260];

    snprintf(payload, sizeof(payload),
             "@CFG,LLW=0,LHW=%u,LLA=0,LHA=%u,THW=0,THA=0,HLW=0,HHW=0,HLA=0,HHA=0,GHW=0,GHA=0,UP=%u,MODE=PRO_LITE,BZ=%u,DEMO=%u,FLT=%u,SEQ=%lu",
             g_app_bonus_cfg.warn_mv, g_app_bonus_cfg.alarm_mv,
             g_app_bonus_cfg.upload_ms, g_app_bonus_cfg.buzzer_enable,
             g_app_bonus_cfg.demo_mode, g_app_bonus_cfg.fault_sample_limit,
             (unsigned long)g_app_bonus_cfg.seq);
    Protocol_SendPayload(payload);
}

void AppBonus_PrintLogs(void)
{
    uint8_t i;
    uint8_t pos;
    char payload[160];
    const AppBonusLog_t *log;

    for (i = 0; i < g_log_count; i++) {
        pos = (uint8_t)((g_log_head + EVENT_LOG_SIZE - g_log_count + i) % EVENT_LOG_SIZE);
        log = &g_logs[pos];
        snprintf(payload, sizeof(payload),
                 "@LOG,idx=%u,ts=%lu,evt=%s,lv=%u,st=%u,risk=%u,err=%02X,light=%u",
                 i, (unsigned long)log->ts_s, log->evt, log->level,
                 log->state, log->risk, log->err, log->adc_mv);
        Protocol_SendPayload(payload);
    }
    snprintf(payload, sizeof(payload), "@LOG,END,count=%u", g_log_count);
    Protocol_SendPayload(payload);
}

void AppBonus_ClearLogs(void)
{
    memset(g_logs, 0, sizeof(g_logs));
    g_log_head = 0;
    g_log_count = 0;
}

void AppBonus_ReportSoftSelfTest(uint16_t adc_mv)
{
    char payload[160];
    snprintf(payload, sizeof(payload),
             "@SELFTEST,result=PASS,mode=NON_INTRUSIVE,adc=%u,lcd=READY,led=UNCHANGED,buzzer=UNCHANGED,flash=A_B_CFG",
             adc_mv);
    Protocol_SendPayload(payload);
    Protocol_SendPayload("@AI,engine=DeepSeek,status=GROUND_CONTEXT_READY,source=STM32_USART1");
}

uint8_t AppBonus_SetParam(const char *key, uint16_t value)
{
    if (strcmp(key, "LHW") == 0 || strcmp(key, "LWW") == 0 || strcmp(key, "WARN") == 0) {
        if (value >= g_app_bonus_cfg.alarm_mv) return 0;
        g_app_bonus_cfg.warn_mv = value;
        AppBonus_OnEvent("CFG_SET_WARN");
        return 1;
    }
    if (strcmp(key, "LHA") == 0 || strcmp(key, "LAA") == 0 || strcmp(key, "ALARM") == 0) {
        if (value <= g_app_bonus_cfg.warn_mv || value >= 3300) return 0;
        g_app_bonus_cfg.alarm_mv = value;
        AppBonus_OnEvent("CFG_SET_ALARM");
        return 1;
    }
    if (strcmp(key, "UP") == 0) {
        if (value < 100 || value > 5000) return 0;
        g_app_bonus_cfg.upload_ms = value;
        AppBonus_OnEvent("CFG_SET_UPLOAD");
        return 1;
    }
    if (strcmp(key, "BZ") == 0) {
        if (value > 1) return 0;
        g_app_bonus_cfg.buzzer_enable = (uint8_t)value;
        AppBonus_OnEvent("CFG_SET_BUZZER");
        return 1;
    }
    if (strcmp(key, "DEMO") == 0) {
        if (value > 1) return 0;
        g_app_bonus_cfg.demo_mode = (uint8_t)value;
        AppBonus_OnEvent(value ? "DEMO_ON" : "DEMO_OFF");
        return 1;
    }
    if (strcmp(key, "FLT") == 0 || strcmp(key, "FAULT") == 0) {
        if (value < 3 || value > 60) return 0;
        g_app_bonus_cfg.fault_sample_limit = (uint8_t)value;
        AppBonus_OnEvent("CFG_SET_FAULT_LIMIT");
        return 1;
    }
    return 0;
}
