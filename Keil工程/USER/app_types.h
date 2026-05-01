#ifndef _APP_TYPES_H
#define _APP_TYPES_H

#include "stm32f10x.h"

#define KEY_EVT_NONE          0
#define KEY_EVT_K1_SHORT      1
#define KEY_EVT_K2_SHORT      2
#define KEY_EVT_K3_SHORT      3
#define KEY_EVT_K4_SHORT      4
#define KEY_EVT_K1_LONG       5
#define KEY_EVT_K1_K4_LONG    6

// 应用数据结构定义
typedef struct {
	uint16_t light_mv;        // 光照电压(mV)
	uint16_t gas_mv;          // 气体模拟值
	uint8_t  temp_c;          // 温度(℃)
	uint8_t  humi_rh;         // 湿度(%)
	
	uint8_t view_page;        // 当前显示页面 0:光照 1:温度 2:湿度 3:气体
	uint8_t sys_state;        // 系统状态 0:正常 1:报警
	uint8_t alarm_level;      // 报警级别 0:L0 1:L1 2:L2 3:L3
	
	uint16_t no_key_alarm_ms; // 报警后无按键时间(ms)
	uint8_t  key_event;       // 按键事件
	uint8_t  alarm_stable_cnt;// 报警稳定计数
} AppData_t;

#endif
