#include "app_eventlog.h"
#include <stdio.h>
#include <string.h>

// 事件日志环形缓冲区
static EventLog_t event_logs[EVENT_LOG_SIZE];
static uint8_t log_head = 0;
static uint8_t log_count = 0;

// 统计信息
static AppStats_t stats;

// 软件时间戳（秒）
static volatile uint32_t sys_timestamp = 0;

static uint8_t AppProtocol_CalcChecksum(const char *payload)
{
	uint8_t cs = 0;
	while(*payload && *payload != '*') {
		cs ^= (uint8_t)(*payload++);
	}
	return cs;
}

void AppProtocol_SendPayload(const char *payload)
{
	printf("%s*%02X\r\n", payload, AppProtocol_CalcChecksum(payload));
}

// 事件名称表
static const char* event_names[] = {
	"BOOT",
	"WARN_ENTER",
	"ALARM_ENTER",
	"LEVEL_AUTO_UP",
	"LEVEL_MANUAL_UP",
	"LEVEL_MANUAL_DOWN",
	"RECOVER",
	"SENSOR_FAULT",
	"CFG_SAVE",
	"WDG_RESET",
	"MODE_SWITCH"
};

// 获取时间戳
uint32_t App_GetTimestamp(void)
{
	return sys_timestamp;
}

// 时间戳递增（每秒调用一次）
void App_IncTimestamp(void)
{
	sys_timestamp++;
	stats.run_time_s++;
}

// 初始化事件日志
void AppEventLog_Init(void)
{
	memset(event_logs, 0, sizeof(event_logs));
	log_head = 0;
	log_count = 0;
}

// 添加事件
void AppEventLog_Add(EventId_t evt, const EnvRuntime_t *rt)
{
	EventLog_t *log = &event_logs[log_head];
	
	log->ts_s = App_GetTimestamp();
	log->evt = evt;
	log->level = rt->alarm_level;
	log->src_bitmap = *(uint8_t*)&rt->src;
	log->state = rt->sys_state;
	log->light_mv = rt->light_mv;
	log->gas_mv = rt->gas_mv;
	log->temp_c = rt->temp_c;
	log->humi_rh = rt->humi_rh;
	
	log_head = (log_head + 1) % EVENT_LOG_SIZE;
	if(log_count < EVENT_LOG_SIZE) {
		log_count++;
	}
}

// 获取日志数量
uint8_t AppEventLog_GetCount(void)
{
	return log_count;
}

// 获取指定索引的日志
const EventLog_t* AppEventLog_Get(uint8_t index)
{
	if(index >= log_count) {
		return NULL;
	}
	
	uint8_t pos = (log_head + EVENT_LOG_SIZE - log_count + index) % EVENT_LOG_SIZE;
	return &event_logs[pos];
}

// 清空日志
void AppEventLog_Clear(void)
{
	log_head = 0;
	log_count = 0;
}

// 打印单条日志
void AppEventLog_Print(uint8_t index)
{
	const EventLog_t *log = AppEventLog_Get(index);
	char payload[180];
	if(log == NULL) return;
	
	snprintf(payload, sizeof(payload), "@LOG,idx=%d,ts=%lu,evt=%s,lv=%d,src=%02X,st=%d,light=%d,temp=%d,humi=%d,gas=%d",
		index, (unsigned long)log->ts_s, event_names[log->evt], log->level,
		log->src_bitmap, log->state, log->light_mv,
		log->temp_c, log->humi_rh, log->gas_mv);
	AppProtocol_SendPayload(payload);
}

// 打印所有日志
void AppEventLog_PrintAll(void)
{
	uint8_t i;
	for(i = 0; i < log_count; i++) {
		AppEventLog_Print(i);
	}
	{
		char payload[32];
		snprintf(payload, sizeof(payload), "@LOG,END,count=%d", log_count);
		AppProtocol_SendPayload(payload);
	}
}

// 初始化统计
void AppStats_Init(void)
{
	memset(&stats, 0, sizeof(stats));
	stats.light_min = 0xFFFF;
}

// 更新统计
void AppStats_Update(const EnvRuntime_t *rt)
{
	// 更新光照最值
	if(rt->light_mv > stats.light_max) {
		stats.light_max = rt->light_mv;
	}
	if(rt->light_mv < stats.light_min) {
		stats.light_min = rt->light_mv;
	}
	
	// 更新最高级别
	if(rt->alarm_level > stats.max_level_ever) {
		stats.max_level_ever = rt->alarm_level;
	}
	
	// 状态计数（状态变化时由外部调用）
}

// 获取统计信息
const AppStats_t* AppStats_Get(void)
{
	return &stats;
}

// 打印统计信息
void AppStats_Print(void)
{
	char payload[140];
	snprintf(payload, sizeof(payload), "@STAT,ALARM_TOTAL=%d,WARN_TOTAL=%d,FAULT_TOTAL=%d,MAX_LEVEL=%d,L_MAX=%d,L_MIN=%d,RUN=%lu",
		stats.alarm_total, stats.warn_total, stats.fault_total,
		stats.max_level_ever, stats.light_max, stats.light_min,
		(unsigned long)stats.run_time_s);
	AppProtocol_SendPayload(payload);
}

// 统计计数增加（由外部调用）
void AppStats_IncAlarm(void) { stats.alarm_total++; }
void AppStats_IncWarn(void) { stats.warn_total++; }
void AppStats_IncFault(void) { stats.fault_total++; }
