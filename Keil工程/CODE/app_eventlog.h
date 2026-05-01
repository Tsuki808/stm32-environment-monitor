#ifndef _APP_EVENTLOG_H
#define _APP_EVENTLOG_H

#include "stm32f10x.h"
#include "app_types_pro.h"

// 日志缓冲区大小
#define EVENT_LOG_SIZE  32

// 函数声明
void AppEventLog_Init(void);
void AppEventLog_Add(EventId_t evt, const EnvRuntime_t *rt);
uint8_t AppEventLog_GetCount(void);
const EventLog_t* AppEventLog_Get(uint8_t index);
void AppEventLog_Clear(void);
void AppEventLog_Print(uint8_t index);
void AppEventLog_PrintAll(void);
void AppProtocol_SendPayload(const char *payload);

// 统计函数
void AppStats_Init(void);
void AppStats_Update(const EnvRuntime_t *rt);
const AppStats_t* AppStats_Get(void);
void AppStats_Print(void);
void AppStats_IncAlarm(void);
void AppStats_IncWarn(void);
void AppStats_IncFault(void);

// 时间戳函数（简化版：使用软件秒计数）
uint32_t App_GetTimestamp(void);
void App_IncTimestamp(void);

#endif
