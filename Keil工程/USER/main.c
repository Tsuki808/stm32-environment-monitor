#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sys.h"
#include "delay.h"
#include "led.h"
#include "key.h"
#include "usart.h"
#include "dht11.h"
#include "timer.h"
#include "ADC_LDR.h"
#include "display.h"
#include "beep_init.h"
#include "app_types.h"
#include "app_types_pro.h"
#include "app_config.h"
#include "app_fusion.h"
#include "app_eventlog.h"
#include "app_diag.h"

// Basic模式数据（保持兼容）
AppData_t g_app;

// Pro模式数据
EnvRuntime_t g_rt;

// ========== 时间节拍标志 ==========
volatile uint8_t g_flag_10ms = 0;
volatile uint8_t g_flag_100ms = 0;
volatile uint8_t g_flag_500ms = 0;
volatile uint8_t g_flag_2000ms = 0;

#define SET_PARAM_COUNT 7

static uint8_t g_setting_mode = 0;
static uint8_t g_setting_index = 0;
static uint8_t g_wdg_seen_10ms = 0;
static uint8_t g_wdg_seen_100ms = 0;
static uint8_t g_wdg_seen_500ms = 0;
static uint8_t g_wdg_seen_2000ms = 0;

// ========== 光照阈值定义 ==========
#define LIGHT_NORMAL_MIN  1100  // 正常区下限(mV)
#define LIGHT_NORMAL_MAX  2200  // 正常区上限(mV)
#define LIGHT_ALARM_MIN   1000  // 报警进入下限(mV)
#define LIGHT_ALARM_MAX   2300  // 报警进入上限(mV)

// ========== 串口重定向 ==========
int fputc(int ch, FILE *f)
{
	USART_SendData(USART1, ch);
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
	return ch;
}

static uint8_t Protocol_CalcChecksum(const char *payload)
{
	uint8_t cs = 0;
	while(*payload && *payload != '*') {
		cs ^= (uint8_t)(*payload++);
	}
	return cs;
}

static uint8_t Protocol_HexVal(char ch)
{
	if(ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
	if(ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10);
	if(ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10);
	return 0xFF;
}

static uint8_t AppConfig_IsSetKey(const char *key)
{
	return (strcmp(key, "LLA") == 0) ||
	       (strcmp(key, "LHA") == 0) ||
	       (strcmp(key, "THA") == 0) ||
	       (strcmp(key, "GHA") == 0) ||
	       (strcmp(key, "UP") == 0) ||
	       (strcmp(key, "BZ") == 0);
}

static void Protocol_SendPayload(const char *payload)
{
	printf("%s*%02X\r\n", payload, Protocol_CalcChecksum(payload));
}

static void Protocol_SendEvent(const char *event)
{
	char payload[96];
	snprintf(payload, sizeof(payload), "@EVT,msg=%s", event);
	Protocol_SendPayload(payload);
}

static void App_WatchdogFeedIfHealthy(void)
{
	if(g_wdg_seen_10ms && g_wdg_seen_100ms && g_wdg_seen_500ms && g_wdg_seen_2000ms) {
		AppDiag_IWDG_Feed();
		g_wdg_seen_10ms = 0;
		g_wdg_seen_100ms = 0;
		g_wdg_seen_500ms = 0;
		g_wdg_seen_2000ms = 0;
	}
}

static void Protocol_SendConfig(void)
{
	char payload[260];
	snprintf(payload, sizeof(payload), "@CFG,LLW=%d,LHW=%d,LLA=%d,LHA=%d,THW=%d,THA=%d,HLW=%d,HHW=%d,HLA=%d,HHA=%d,GHW=%d,GHA=%d,UP=%d,MODE=%s,BZ=%d",
		g_config.light_low_warn, g_config.light_high_warn,
		g_config.light_low_alarm, g_config.light_high_alarm,
		g_config.temp_high_warn, g_config.temp_high_alarm,
		g_config.humi_low_warn, g_config.humi_high_warn,
		g_config.humi_low_alarm, g_config.humi_high_alarm,
		g_config.gas_high_warn, g_config.gas_high_alarm,
		g_config.upload_period_ms,
		(g_config.default_mode == MODE_PRO) ? "PRO" : "BASIC",
		g_config.buzzer_enable);
	Protocol_SendPayload(payload);
}

static uint8_t Protocol_UnwrapCommand(const char *in, char *out, uint8_t out_size)
{
	const char *start;
	const char *star;
	uint8_t len;
	uint8_t hi, lo, rx_cs, calc_cs;
	
	if(strncmp(in, "@CMD,", 5) != 0) {
		Protocol_SendPayload("@NACK,cmd=CMD,err=BAD_FRAME");
		return 0;
	}
	
	star = strchr(in, '*');
	if(star == NULL || star[1] == '\0' || star[2] == '\0') {
		Protocol_SendPayload("@NACK,err=BAD_FRAME");
		return 0;
	}
	
	hi = Protocol_HexVal(star[1]);
	lo = Protocol_HexVal(star[2]);
	if(hi == 0xFF || lo == 0xFF) {
		Protocol_SendPayload("@NACK,cmd=CMD,err=BAD_CS");
		return 0;
	}
	rx_cs = (uint8_t)((hi << 4) | lo);
	calc_cs = Protocol_CalcChecksum(in);
	if(rx_cs != calc_cs) {
		Protocol_SendPayload("@NACK,cmd=CMD,err=BAD_CS");
		return 0;
	}
	
	start = in + 5;
	len = (uint8_t)(star - start);
	if(len >= out_size) len = out_size - 1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 1;
}

static uint8_t AppConfig_SetByKey(const char *key, uint16_t value)
{
	if(strcmp(key, "LLA") == 0) {
		if(value >= g_config.light_high_alarm) return 0;
		g_config.light_low_alarm = value;
		g_config.light_low_warn = value + 200;
	} else if(strcmp(key, "LHA") == 0) {
		if(value <= g_config.light_low_alarm || value > 3300) return 0;
		g_config.light_high_alarm = value;
		g_config.light_high_warn = (value >= 200) ? (value - 200) : value;
	} else if(strcmp(key, "THA") == 0) {
		if(value > 80) return 0;
		g_config.temp_high_alarm = value;
		g_config.temp_high_warn = (value > 5) ? (value - 5) : value;
	} else if(strcmp(key, "GHA") == 0) {
		if(value > 3300) return 0;
		g_config.gas_high_alarm = value;
		g_config.gas_high_warn = (value >= 400) ? (value - 400) : value;
	} else if(strcmp(key, "UP") == 0) {
		if(value < 100 || value > 5000) return 0;
		g_config.upload_period_ms = value;
	} else if(strcmp(key, "BZ") == 0) {
		if(value > 1) return 0;
		g_config.buzzer_enable = (uint8_t)value;
	} else {
		return 0;
	}
	return 1;
}

static uint8_t AppConfig_ParseSetCommand(const char *cmd, char *key_out, uint16_t *value_out)
{
	const char *key_start;
	const char *eq;
	char *endptr;
	unsigned long parsed_value;
	uint8_t len;
	
	if(strncmp(cmd, "SET,", 4) != 0) return 0;
	key_start = cmd + 4;
	eq = strchr(key_start, '=');
	if(eq == NULL) return 0;
	len = (uint8_t)(eq - key_start);
	if(len == 0 || len > 3) return 0;
	memcpy(key_out, key_start, len);
	key_out[len] = '\0';
	parsed_value = strtoul(eq + 1, &endptr, 10);
	if(endptr == eq + 1 || *endptr != '\0' || parsed_value > 65535UL) return 0;
	*value_out = (uint16_t)parsed_value;
	return 1;
}

static void AppConfig_PrintCurrentParam(void)
{
	char payload[96];
	switch(g_setting_index) {
		case 0: snprintf(payload, sizeof(payload), "@SET,IDX=0,NAME=LIGHT_LOW_ALARM,VAL=%d", g_config.light_low_alarm); break;
		case 1: snprintf(payload, sizeof(payload), "@SET,IDX=1,NAME=LIGHT_HIGH_ALARM,VAL=%d", g_config.light_high_alarm); break;
		case 2: snprintf(payload, sizeof(payload), "@SET,IDX=2,NAME=TEMP_HIGH_ALARM,VAL=%d", g_config.temp_high_alarm); break;
		case 3: snprintf(payload, sizeof(payload), "@SET,IDX=3,NAME=GAS_HIGH_ALARM,VAL=%d", g_config.gas_high_alarm); break;
		case 4: snprintf(payload, sizeof(payload), "@SET,IDX=4,NAME=UPLOAD_PERIOD_MS,VAL=%d", g_config.upload_period_ms); break;
		case 5: snprintf(payload, sizeof(payload), "@SET,IDX=5,NAME=MODE,VAL=%s", g_config.default_mode == MODE_PRO ? "PRO" : "BASIC"); break;
		case 6: snprintf(payload, sizeof(payload), "@SET,IDX=6,NAME=BUZZER_ENABLE,VAL=%d", g_config.buzzer_enable); break;
		default: break;
	}
	if(g_setting_index < SET_PARAM_COUNT) Protocol_SendPayload(payload);
}

static void AppConfig_AdjustCurrentParam(int8_t delta)
{
	switch(g_setting_index) {
		case 0:
			if(delta > 0 && g_config.light_low_alarm < g_config.light_high_alarm - 50) g_config.light_low_alarm += 50;
			else if(delta < 0 && g_config.light_low_alarm >= 50) g_config.light_low_alarm -= 50;
			g_config.light_low_warn = g_config.light_low_alarm + 200;
			break;
		case 1:
			if(delta > 0 && g_config.light_high_alarm <= 3250) g_config.light_high_alarm += 50;
			else if(delta < 0 && g_config.light_high_alarm > g_config.light_low_alarm + 50) g_config.light_high_alarm -= 50;
			g_config.light_high_warn = (g_config.light_high_alarm >= 200) ? (g_config.light_high_alarm - 200) : g_config.light_high_alarm;
			break;
		case 2:
			if(delta > 0 && g_config.temp_high_alarm < 80) g_config.temp_high_alarm++;
			else if(delta < 0 && g_config.temp_high_alarm > 1) g_config.temp_high_alarm--;
			g_config.temp_high_warn = (g_config.temp_high_alarm > 5) ? (g_config.temp_high_alarm - 5) : g_config.temp_high_alarm;
			break;
		case 3:
			if(delta > 0 && g_config.gas_high_alarm <= 3250) g_config.gas_high_alarm += 50;
			else if(delta < 0 && g_config.gas_high_alarm >= 50) g_config.gas_high_alarm -= 50;
			g_config.gas_high_warn = (g_config.gas_high_alarm >= 400) ? (g_config.gas_high_alarm - 400) : g_config.gas_high_alarm;
			break;
		case 4:
			if(delta > 0 && g_config.upload_period_ms < 5000) g_config.upload_period_ms += 100;
			else if(delta < 0 && g_config.upload_period_ms > 100) g_config.upload_period_ms -= 100;
			break;
		case 5:
			g_config.default_mode = (g_config.default_mode == MODE_BASIC) ? MODE_PRO : MODE_BASIC;
			g_rt.mode = g_config.default_mode;
			break;
		case 6:
			g_config.buzzer_enable = !g_config.buzzer_enable;
			break;
		default:
			break;
	}
	AppConfig_PrintCurrentParam();
}

static void Setting_Enter(void)
{
	g_setting_mode = 1;
	g_setting_index = 0;
	Protocol_SendEvent("SET_ENTER");
	AppConfig_PrintCurrentParam();
}

static void Setting_SaveExit(void)
{
	g_setting_mode = 0;
	g_rt.mode = g_config.default_mode;
	if(AppConfig_Save(&g_config)) {
		AppEventLog_Add(EVT_CFG_SAVE, &g_rt);
		Protocol_SendEvent("SET_SAVED_EXIT");
	} else {
		Protocol_SendEvent("SET_SAVE_FAILED");
	}
}

static void Setting_FactoryRestore(void)
{
	AppConfig_SetDefault(&g_config);
	g_rt.mode = g_config.default_mode;
	g_setting_mode = 0;
	if(AppConfig_Save(&g_config)) {
		AppEventLog_Add(EVT_CFG_SAVE, &g_rt);
		Protocol_SendEvent("CFG_DEFAULT_RESTORED");
	} else {
		Protocol_SendEvent("CFG_DEFAULT_RESTORE_FAILED");
	}
}

// ========== 串口命令处理 ==========
void UART_ProcessCommand(char *cmd)
{
	char parsed[128];
	char set_key[4];
	uint16_t set_value;
	char payload[64];
	
	if(!Protocol_UnwrapCommand(cmd, parsed, sizeof(parsed))) {
		return;
	}
	
	if(strcmp(parsed, "STAT?") == 0) {
		AppStats_Print();
	}
	else if(strcmp(parsed, "LOG?") == 0) {
		AppEventLog_PrintAll();
	}
	else if(strcmp(parsed, "LOGCLR") == 0 || strcmp(parsed, "CLRLOG") == 0) {
		AppEventLog_Clear();
		Protocol_SendPayload("@ACK,cmd=CLRLOG");
	}
	else if(strcmp(parsed, "CFG?") == 0) {
		Protocol_SendConfig();
	}
	else if(AppConfig_ParseSetCommand(parsed, set_key, &set_value)) {
		if(!AppConfig_IsSetKey(set_key)) {
			snprintf(payload, sizeof(payload), "@NACK,cmd=SET,err=BAD_KEY,key=%s", set_key);
			Protocol_SendPayload(payload);
		} else if(AppConfig_SetByKey(set_key, set_value)) {
			snprintf(payload, sizeof(payload), "@ACK,cmd=SET,key=%s,val=%d", set_key, set_value);
			Protocol_SendPayload(payload);
		} else {
			snprintf(payload, sizeof(payload), "@NACK,cmd=SET,err=OUT_OF_RANGE,key=%s", set_key);
			Protocol_SendPayload(payload);
		}
	}
	else if(strcmp(parsed, "MODE=BASIC") == 0) {
		g_rt.mode = MODE_BASIC;
		g_config.default_mode = MODE_BASIC;
		AppEventLog_Add(EVT_MODE_SWITCH, &g_rt);
		Protocol_SendPayload("@ACK,cmd=MODE=BASIC");
	}
	else if(strcmp(parsed, "MODE=PRO") == 0) {
		g_rt.mode = MODE_PRO;
		g_config.default_mode = MODE_PRO;
		AppEventLog_Add(EVT_MODE_SWITCH, &g_rt);
		Protocol_SendPayload("@ACK,cmd=MODE=PRO");
	}
	else if(strcmp(parsed, "SAVE") == 0) {
		if(AppConfig_Save(&g_config)) {
			AppEventLog_Add(EVT_CFG_SAVE, &g_rt);
			Protocol_SendPayload("@ACK,cmd=SAVE");
		} else {
			Protocol_SendPayload("@NACK,err=SAVE_FAILED");
		}
	}
	else if(strcmp(parsed, "RESET") == 0 || strcmp(parsed, "DEFAULT") == 0) {
		AppConfig_SetDefault(&g_config);
		g_rt.mode = g_config.default_mode;
		if(AppConfig_Save(&g_config)) {
			AppEventLog_Add(EVT_CFG_SAVE, &g_rt);
			Protocol_SendPayload("@ACK,cmd=DEFAULT");
		} else {
			Protocol_SendPayload("@NACK,err=SAVE_FAILED");
		}
	}
	else {
		Protocol_SendPayload("@NACK,cmd=CMD,err=UNKNOWN_CMD");
	}
}

// ========== 按键事件处理 ==========
void Key_ProcessEvent(void)
{
	char payload[80];

	if(g_app.key_event == KEY_EVT_NONE) return;
	
	if(g_app.key_event == KEY_EVT_K1_K4_LONG) {
		Setting_FactoryRestore();
		g_app.key_event = KEY_EVT_NONE;
		g_rt.key_event = KEY_EVT_NONE;
		return;
	}
	
	if(g_setting_mode) {
		switch(g_app.key_event) {
			case KEY_EVT_K1_LONG:
				Setting_SaveExit();
				break;
			case KEY_EVT_K2_SHORT:
				AppConfig_AdjustCurrentParam(1);
				break;
			case KEY_EVT_K3_SHORT:
				AppConfig_AdjustCurrentParam(-1);
				break;
			case KEY_EVT_K4_SHORT:
				g_setting_index = (g_setting_index + 1) % SET_PARAM_COUNT;
				AppConfig_PrintCurrentParam();
				break;
			default:
				break;
		}
		g_app.key_event = KEY_EVT_NONE;
		g_rt.key_event = KEY_EVT_NONE;
		return;
	}
	
	switch(g_app.key_event) {
		case KEY_EVT_K1_SHORT: // K1: 切换显示页面
			g_app.view_page = (g_app.view_page + 1) % 4;
			g_rt.view_page = g_app.view_page;
			snprintf(payload, sizeof(payload), "@EVT,msg=PAGE,val=%d", g_app.view_page);
			Protocol_SendPayload(payload);
			break;
			
		case KEY_EVT_K1_LONG: // K1长按: 进入设置菜单
			Setting_Enter();
			break;
			
		case KEY_EVT_K2_SHORT: // K2: 报警级别+1
			if(g_app.sys_state == 1 && g_app.alarm_level < 3) {
				g_app.alarm_level++;
				g_rt.alarm_level = g_app.alarm_level;
				g_app.no_key_alarm_ms = 0;
				g_rt.no_key_alarm_ms = 0;
				AppEventLog_Add(EVT_LEVEL_MANUAL_UP, &g_rt);
				snprintf(payload, sizeof(payload), "@EVT,msg=LEVEL_MANUAL_UP,level=%d", g_app.alarm_level);
				Protocol_SendPayload(payload);
			}
			break;
			
		case KEY_EVT_K3_SHORT: // K3: 报警级别-1
			if(g_app.sys_state == 1 && g_app.alarm_level > 1) {
				g_app.alarm_level--;
				g_rt.alarm_level = g_app.alarm_level;
				g_app.no_key_alarm_ms = 0;
				g_rt.no_key_alarm_ms = 0;
				AppEventLog_Add(EVT_LEVEL_MANUAL_DOWN, &g_rt);
				snprintf(payload, sizeof(payload), "@EVT,msg=LEVEL_MANUAL_DOWN,level=%d", g_app.alarm_level);
				Protocol_SendPayload(payload);
			}
			break;
			
		case KEY_EVT_K4_SHORT: // K4: 复位2秒计时器
			if(g_app.sys_state == 1) {
				g_app.no_key_alarm_ms = 0;
				g_rt.no_key_alarm_ms = 0;
				Protocol_SendEvent("TIMER_RESET");
			}
			break;
	}
	
	g_app.key_event = KEY_EVT_NONE;
	g_rt.key_event = KEY_EVT_NONE;
}

// ========== 报警逻辑处理（Basic模式） ==========
void Alarm_Process_Basic(void)
{
	char payload[80];
	// 判断是否进入异常区
	uint8_t is_abnormal = (g_app.light_mv < g_config.light_low_alarm) || 
	                      (g_app.light_mv > g_config.light_high_alarm);
	
	if(is_abnormal) {
		g_app.alarm_stable_cnt = 0;
		g_rt.alarm_stable_cnt = 0;
		
		// 进入或保持报警状态
		if(g_app.sys_state == 0) {
			g_app.sys_state = 1;
			g_app.alarm_level = 1;
			g_app.no_key_alarm_ms = 0;
			
			// 同步到Pro模式数据
			g_rt.sys_state = SYS_ALARM;
			g_rt.alarm_level = ALARM_L1;
			g_rt.no_key_alarm_ms = 0;
			
			AppEventLog_Add(EVT_ALARM_ENTER, &g_rt);
			Protocol_SendEvent("ALARM_ENTER_L1");
			AppStats_IncAlarm();
		}
		
		// 2秒自动升级逻辑
		if(g_app.no_key_alarm_ms >= 2000 && g_app.alarm_level < 3) {
			g_app.alarm_level++;
			g_rt.alarm_level = g_app.alarm_level;
			g_app.no_key_alarm_ms = 0;
			g_rt.no_key_alarm_ms = 0;
			
			AppEventLog_Add(EVT_LEVEL_AUTO_UP, &g_rt);
			snprintf(payload, sizeof(payload), "@EVT,msg=LEVEL_AUTO_UP,level=%d", g_app.alarm_level);
			Protocol_SendPayload(payload);
		}
	} else {
		// 判断是否恢复正常
		if(g_app.sys_state == 1) {
			if(g_app.light_mv >= g_config.light_low_warn && g_app.light_mv <= g_config.light_high_warn) {
				g_app.alarm_stable_cnt++;
				if(g_app.alarm_stable_cnt >= 5) {
					g_app.sys_state = 0;
					g_app.alarm_level = 0;
					g_app.alarm_stable_cnt = 0;
					
					// 同步到Pro模式数据
					g_rt.sys_state = SYS_NORMAL;
					g_rt.alarm_level = ALARM_L0;
					g_rt.alarm_stable_cnt = 0;
					
					AppEventLog_Add(EVT_RECOVER, &g_rt);
					Protocol_SendEvent("RECOVER");
				}
			} else {
				g_app.alarm_stable_cnt = 0;
			}
		}
	}
}

// ========== 报警逻辑处理（Pro模式） ==========
void Alarm_Process_Pro(void)
{
	uint8_t old_state = g_rt.sys_state;
	uint8_t old_level = g_rt.alarm_level;
	
	// 调用多源融合处理
	AppFusion_Process(&g_rt, &g_config);
	
	// 同步到Basic模式数据
	if(g_rt.sys_state == SYS_NORMAL) {
		g_app.sys_state = 0;
		g_app.alarm_level = 0;
	} else if(g_rt.sys_state == SYS_WARN) {
		g_app.sys_state = 0;  // Basic模式无预警状态
		g_app.alarm_level = 0;
	} else if(g_rt.sys_state == SYS_ALARM || g_rt.sys_state == SYS_FAULT || g_rt.sys_state == SYS_FAULT_LOCK) {
		g_app.sys_state = 1;
		g_app.alarm_level = g_rt.alarm_level;
	}
	
	// 记录状态变化事件
	if(old_state != g_rt.sys_state) {
		if(g_rt.sys_state == SYS_WARN) {
			AppEventLog_Add(EVT_WARN_ENTER, &g_rt);
			AppStats_IncWarn();
		} else if(g_rt.sys_state == SYS_ALARM) {
			AppEventLog_Add(EVT_ALARM_ENTER, &g_rt);
			AppStats_IncAlarm();
		} else if(g_rt.sys_state == SYS_FAULT || g_rt.sys_state == SYS_FAULT_LOCK) {
			AppEventLog_Add(EVT_SENSOR_FAULT, &g_rt);
			AppStats_IncFault();
		} else if(g_rt.sys_state == SYS_NORMAL) {
			AppEventLog_Add(EVT_RECOVER, &g_rt);
		}
	}
	
	// 记录级别自动升级事件
	if(old_level < g_rt.alarm_level && g_rt.sys_state == SYS_ALARM) {
		AppEventLog_Add(EVT_LEVEL_AUTO_UP, &g_rt);
	}
}

// ========== 串口上传任务 ==========
void Uart_ReportTask(void)
{
	static uint16_t upload_elapsed_ms = 0;
	static uint16_t env_seq = 0;
	char payload[240];
	uint8_t state_index;
	
	uint32_t ms_stamp = App_GetTimestamp() * 1000;
	
	upload_elapsed_ms += 500;
	if(upload_elapsed_ms < g_config.upload_period_ms) {
		return;
	}
	upload_elapsed_ms = 0;
	
	if(g_rt.mode == MODE_BASIC) {
		// Basic模式上传格式
		const char* state_str = (g_app.sys_state == 0) ? "NORMAL" : "ALARM";
		snprintf(payload, sizeof(payload), "@ENV,seq=%d,ms=%lu,mode=BASIC,light=%d,temp=%d,humi=%d,gas=%d,state=%s,level=%d,risk=%d,src=NONE,err=00",
			env_seq++, (unsigned long)ms_stamp, g_app.light_mv, g_app.temp_c, g_app.humi_rh, 
			g_app.gas_mv, state_str, g_app.alarm_level, g_rt.risk_score);
		Protocol_SendPayload(payload);
	} else {
		// Pro模式上传格式（包含更多信息）
		const char* state_names[] = {"NORMAL", "WARN", "ALARM", "FAULT", "FAULT_LOCK"};
		char src_str[64] = {0};
		#define APPEND_SRC(name) do { \
			size_t used = strlen(src_str); \
			if(used > 0 && used + 1 < sizeof(src_str)) strncat(src_str, "|", sizeof(src_str) - used - 1); \
			used = strlen(src_str); \
			if(used + 1 < sizeof(src_str)) strncat(src_str, (name), sizeof(src_str) - used - 1); \
		} while(0)
		if (g_rt.src.light_abn) APPEND_SRC("LIGHT");
		if (g_rt.src.temp_abn)  APPEND_SRC("TEMP");
		if (g_rt.src.humi_abn)  APPEND_SRC("HUMI");
		if (g_rt.src.gas_abn)   APPEND_SRC("GAS");
		if (g_rt.src.dht_fault) APPEND_SRC("DHT_ERR");
		if (g_rt.src.adc_fault) APPEND_SRC("ADC_ERR");
		if (src_str[0] == '\0') snprintf(src_str, sizeof(src_str), "NONE");
		#undef APPEND_SRC
		
		state_index = (g_rt.sys_state <= SYS_FAULT_LOCK) ? g_rt.sys_state : SYS_FAULT_LOCK;
		snprintf(payload, sizeof(payload), "@ENV,seq=%d,ms=%lu,mode=PRO,light=%d,temp=%d,humi=%d,gas=%d,state=%s,level=%d,risk=%d,src=%s,err=%02X",
			env_seq++, (unsigned long)ms_stamp, g_rt.light_mv, g_rt.temp_c, g_rt.humi_rh, g_rt.gas_mv,
			state_names[state_index], g_rt.alarm_level, g_rt.risk_score, src_str, *(uint8_t*)&g_rt.src);
		Protocol_SendPayload(payload);
	}
}

// ========== 10ms任务 ==========
void App_Process10msTasks(void)
{
	if(!g_flag_10ms) return;
	g_flag_10ms = 0;
	Key_Scan10ms();
	g_wdg_seen_10ms = 1;
}

// ========== 100ms任务 ==========
void App_Process100msTasks(void)
{
	if(!g_flag_100ms) return;
	g_flag_100ms = 0;
	
	// ADC采样 (DMA + 中值平滑滤波)
	ADC_Update_Filtered(&g_app.light_mv, &g_app.gas_mv);
	
	// 同步到Pro模式数据
	g_rt.light_mv = g_app.light_mv;
	g_rt.gas_mv = g_app.gas_mv;
	
	// 故障检测：ADC全0检测
	if(g_app.light_mv == 0 && g_app.gas_mv == 0) {
		g_rt.adc_zero_cnt++;
	} else {
		g_rt.adc_zero_cnt = 0;
	}
	
	// 报警时间累加
	if(g_app.sys_state == 1) {
		g_app.no_key_alarm_ms += 100;
		g_rt.no_key_alarm_ms += 100;
	}
	
	// 更新统计信息
	AppStats_Update(&g_rt);
	g_wdg_seen_100ms = 1;
}

// ========== 500ms任务 ==========
void App_Process500msTasks(void)
{
	static uint8_t cnt_1s = 0;
	
	if(!g_flag_500ms) return;
	g_flag_500ms = 0;
	
	cnt_1s++;
	if(cnt_1s >= 2) {
		cnt_1s = 0;
		// 1秒任务：传感器冻结检测
		AppDiag_CheckFrozen(&g_rt);
	}
	
	// 调用串口上传，这样就共享了500ms的执行频率
	Uart_ReportTask();
	g_wdg_seen_500ms = 1;
}

// ========== 2s任务 ==========
void App_Process2sTasks(void)
{
	if(!g_flag_2000ms) return;
	g_flag_2000ms = 0;
	
	// DHT11采样
	uint8_t ret = Read_DHT11_Data_Blocking(&g_app.temp_c, &g_app.humi_rh);
	
	// 同步到Pro模式数据
	g_rt.temp_c = g_app.temp_c;
	g_rt.humi_rh = g_app.humi_rh;
	
	// 故障检测：DHT11失败计数
	if(ret != 0) {
		g_rt.dht_fail_cnt++;
	} else {
		g_rt.dht_fail_cnt = 0;
	}
	g_wdg_seen_2000ms = 1;
}

// ========== 主函数 ==========
int main(void)
{
	// 系统初始化
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	// 外设初始化
	LED_Init();
	beep_init();
	KEY_Init();
	USART_Config();
	display_Init();
	ADC_Init_Continuous();
	TIM2_SystemTick_Init();
	
	// Pro模式模块初始化
	AppConfig_Init();        // 加载配置
	AppEventLog_Init();      // 初始化事件日志
	AppStats_Init();         // 初始化统计
	AppFusion_Init();        // 初始化融合模块
	AppDiag_PowerOnSelfTest(); // 上电自检
	AppDiag_IWDG_Init();     // 自检完成后再启动独立看门狗
	// Basic模式数据初始化
	g_app.view_page = 0;
	g_app.sys_state = 0;
	g_app.alarm_level = 0;
	g_app.no_key_alarm_ms = 0;
	g_app.key_event = KEY_EVT_NONE;
	g_app.alarm_stable_cnt = 0;
	g_app.light_mv = 0;
	g_app.gas_mv = 0;
	g_app.temp_c = 0;
	g_app.humi_rh = 0;
	
	// Pro模式数据初始化
	memset(&g_rt, 0, sizeof(g_rt));
	g_rt.mode = g_config.default_mode;  // 从配置读取默认模式
	g_rt.sys_state = SYS_NORMAL;
	g_rt.alarm_level = ALARM_L0;
	
	// 记录启动事件
	AppEventLog_Add(EVT_BOOT, &g_rt);
	if(AppDiag_WasWatchdogReset()) {
		AppEventLog_Add(EVT_WDG_RESET, &g_rt);
	}
	
	{
		char payload[80];
		snprintf(payload, sizeof(payload), "@EVT,msg=BOOT,mode=%s,clk=72MHz", (g_rt.mode == MODE_BASIC) ? "BASIC" : "PRO");
		Protocol_SendPayload(payload);
	}
	
	// 主循环
	while(1)
	{
		App_Process10msTasks();
		App_Process100msTasks();
		App_Process500msTasks();
		App_Process2sTasks();
		USART_PollCommand();
		
		Key_ProcessEvent();
		
		// 根据模式选择报警处理逻辑
		if(g_rt.mode == MODE_BASIC) {
			Alarm_Process_Basic();
		} else {
			Alarm_Process_Pro();
		}
		
		Display_UpdateBuffer(&g_app);
		App_WatchdogFeedIfHealthy();
	}
}
