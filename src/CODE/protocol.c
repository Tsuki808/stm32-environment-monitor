#include "protocol.h"
#include "app_bonus.h"
#include "app_fusion_lite.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 外部 USART 发送函数（来自 qimo）
extern void USART1_SendString(char *s);
extern void USART1_SendChar(char c);

// 序列号计数器
static uint32_t g_seq_counter = 0;
static uint32_t g_ms_counter = 0;
static uint16_t g_report_elapsed = 0;

static uint8_t Protocol_LevelFromState(uint8_t state)
{
    switch ((AlarmState_t)state) {
        case STATE_ALARM_L1: return 1;
        case STATE_ALARM_L2: return 2;
        case STATE_ALARM_L3: return 3;
        case STATE_FAULT:    return 3;
        default:             return 0;
    }
}

static uint8_t Protocol_CalcChecksum(const char *payload)
{
    uint8_t cs = 0;
    while (*payload && *payload != '*') {
        cs ^= (uint8_t)(*payload++);
    }
    return cs;
}

static uint8_t Protocol_HexVal(char ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10);
    return 0xFF;
}

void Protocol_SendPayload(const char *payload)
{
    char frame[300];
    uint8_t cs;

    if (payload == 0) return;
    cs = Protocol_CalcChecksum(payload);
    snprintf(frame, sizeof(frame), "%s*%02X\r\n", payload, cs);
    USART1_SendString(frame);
}

static void Protocol_SendFrame(const char *payload)
{
    Protocol_SendPayload(payload);
}

void Protocol_SendEnvFrame(uint16_t adc_mv, uint8_t level, uint8_t state, uint8_t risk_score)
{
    char payload[240];
    const char *state_str[] = {"NORMAL", "WARN", "ALARM", "ALARM", "ALARM", "FAULT"};
    uint16_t upload_ms;

    g_seq_counter++;
    g_ms_counter += 500;
    AppBonus_OnEnvSample(adc_mv, state, level, risk_score);

    upload_ms = AppBonus_GetUploadMs();
    if (upload_ms < 100) upload_ms = 500;
    g_report_elapsed = (uint16_t)(g_report_elapsed + 500);
    if (g_report_elapsed < upload_ms) return;
    g_report_elapsed = 0;

    snprintf(payload, sizeof(payload),
             "@ENV,seq=%lu,ms=%lu,mode=PRO_LITE,light=%u,temp=0.0,humi=0.0,gas=0,state=%s,level=%u,risk=%u,src=LIGHT,err=%02X",
             (unsigned long)g_seq_counter, (unsigned long)g_ms_counter, adc_mv,
             state < 6 ? state_str[state] : "UNKNOWN", level, risk_score,
             AppBonus_GetErrFlags());

    Protocol_SendFrame(payload);
}

void Protocol_SendEventFrame(const char *event_msg)
{
    char payload[120];

    AppBonus_UpdateStateSnapshot((uint8_t)Fusion_GetState(), Protocol_LevelFromState((uint8_t)Fusion_GetState()), Fusion_GetRiskScore());
    AppBonus_OnEvent(event_msg);
    snprintf(payload, sizeof(payload), "@EVT,msg=%s,ms=%lu", event_msg, (unsigned long)g_ms_counter);
    Protocol_SendFrame(payload);
}

void Protocol_SendResponseFrame(const char *key, const char *value)
{
    char payload[96];

    snprintf(payload, sizeof(payload), "@RESP,%s=%s", key, value);
    Protocol_SendFrame(payload);
}

static uint8_t Protocol_UnwrapCommand(char *cmd, char **body_out)
{
    char *cs_pos;
    uint8_t hi;
    uint8_t lo;
    uint8_t rx_cs;
    uint8_t calc_cs;

    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (strncmp(cmd, "@CMD,", 5) != 0) return 0;

    cs_pos = strchr(cmd, '*');
    if (cs_pos != 0) {
        if (cs_pos[1] == '\0' || cs_pos[2] == '\0') {
            Protocol_SendFrame("@NACK,cmd=CMD,err=BAD_FRAME");
            return 0;
        }
        hi = Protocol_HexVal(cs_pos[1]);
        lo = Protocol_HexVal(cs_pos[2]);
        if (hi == 0xFF || lo == 0xFF) {
            Protocol_SendFrame("@NACK,cmd=CMD,err=BAD_CS");
            return 0;
        }
        rx_cs = (uint8_t)((hi << 4) | lo);
        *cs_pos = '\0';
        calc_cs = Protocol_CalcChecksum(cmd);
        if (rx_cs != calc_cs) {
            Protocol_SendFrame("@NACK,cmd=CMD,err=BAD_CS");
            return 0;
        }
    }

    *body_out = cmd + 5;
    return 1;
}

static uint8_t Protocol_ParseSet(const char *cmd_body, char *key_out, uint16_t *value_out)
{
    const char *key_start;
    const char *eq;
    char *endptr;
    unsigned long value;
    uint8_t len;

    if (strncmp(cmd_body, "SET,", 4) != 0) return 0;
    key_start = cmd_body + 4;
    eq = strchr(key_start, '=');
    if (eq == 0) return 0;
    len = (uint8_t)(eq - key_start);
    if (len == 0 || len > 8) return 0;
    memcpy(key_out, key_start, len);
    key_out[len] = '\0';

    value = strtoul(eq + 1, &endptr, 10);
    if (endptr == eq + 1 || *endptr != '\0' || value > 65535UL) return 0;
    *value_out = (uint16_t)value;
    return 1;
}

void Protocol_ProcessCommand(char *cmd)
{
    char *cmd_body;
    char key[9];
    uint16_t value;
    char payload[120];

    if (!Protocol_UnwrapCommand(cmd, &cmd_body)) return;

    if (strcmp(cmd_body, "STAT?") == 0) {
        AppBonus_PrintStats();
    } else if (strcmp(cmd_body, "CFG?") == 0) {
        AppBonus_PrintConfig();
    } else if (strcmp(cmd_body, "LOG?") == 0) {
        AppBonus_PrintLogs();
    } else if (strcmp(cmd_body, "CLRLOG") == 0 || strcmp(cmd_body, "LOGCLR") == 0) {
        AppBonus_ClearLogs();
        Protocol_SendFrame("@ACK,cmd=CLRLOG");
    } else if (Protocol_ParseSet(cmd_body, key, &value)) {
        if (AppBonus_SetParam(key, value)) {
            snprintf(payload, sizeof(payload), "@ACK,cmd=SET,key=%s,val=%u", key, value);
            Protocol_SendFrame(payload);
        } else {
            snprintf(payload, sizeof(payload), "@NACK,cmd=SET,err=BAD_VALUE,key=%s", key);
            Protocol_SendFrame(payload);
        }
    } else if (strcmp(cmd_body, "SAVE") == 0) {
        if (AppBonus_SaveConfig()) Protocol_SendFrame("@ACK,cmd=SAVE");
        else Protocol_SendFrame("@NACK,cmd=SAVE,err=FLASH_WRITE");
    } else if (strcmp(cmd_body, "RESET") == 0 || strcmp(cmd_body, "DEFAULT") == 0) {
        if (AppBonus_ResetConfig(1)) {
            Protocol_SendFrame("@ACK,cmd=DEFAULT");
            Protocol_SendEventFrame("CFG_DEFAULT_RESTORED");
        } else {
            Protocol_SendFrame("@NACK,cmd=DEFAULT,err=FLASH_WRITE");
        }
    } else if (strcmp(cmd_body, "MODE=PRO_LITE") == 0 || strcmp(cmd_body, "MODE=LITE") == 0) {
        Protocol_SendFrame("@ACK,cmd=MODE=PRO_LITE");
        Protocol_SendEventFrame("MODE_PRO_LITE");
    } else if (strcmp(cmd_body, "DEMO=1") == 0 || strcmp(cmd_body, "DEMO=ON") == 0) {
        AppBonus_SetParam("DEMO", 1);
        Protocol_SendFrame("@ACK,cmd=DEMO,val=1");
        Protocol_SendEventFrame("DEMO_ON");
    } else if (strcmp(cmd_body, "DEMO=0") == 0 || strcmp(cmd_body, "DEMO=OFF") == 0) {
        AppBonus_SetParam("DEMO", 0);
        Protocol_SendFrame("@ACK,cmd=DEMO,val=0");
        Protocol_SendEventFrame("DEMO_OFF");
    } else if (strcmp(cmd_body, "MODE=PRO") == 0 || strcmp(cmd_body, "MODE=BASIC") == 0) {
        Protocol_SendFrame("@NACK,cmd=MODE,err=HW_SINGLE_ADC_ONLY");
    } else {
        Protocol_SendFrame("@NACK,cmd=CMD,err=UNKNOWN_CMD");
    }
}
