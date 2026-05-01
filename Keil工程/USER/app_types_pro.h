#ifndef _APP_TYPES_PRO_H
#define _APP_TYPES_PRO_H

#include "stm32f10x.h"

// ========== 工作模式 ==========
typedef enum {
	MODE_BASIC = 0,  // 基础模式：只看LDR
	MODE_PRO = 1     // Pro模式：多源融合
} WorkMode_t;

// ========== 系统状态（扩展版） ==========
typedef enum {
	SYS_NORMAL = 0,  // 正常
	SYS_WARN = 1,    // 预警
	SYS_ALARM = 2,   // 报警
	SYS_FAULT = 3,   // 故障，可退化运行
	SYS_FAULT_LOCK = 4 // 核心量故障，锁定不可测
} SysState_Pro_t;

// ========== 报警级别 ==========
typedef enum {
	ALARM_L0 = 0,
	ALARM_L1 = 1,
	ALARM_L2 = 2,
	ALARM_L3 = 3
} AlarmLevel_t;

// ========== 报警源位图 ==========
typedef struct {
	uint8_t light_abn  : 1;
	uint8_t temp_abn   : 1;
	uint8_t humi_abn   : 1;
	uint8_t gas_abn    : 1;
	uint8_t dht_fault  : 1;
	uint8_t adc_fault  : 1;
	uint8_t reserved   : 2;
} AlarmSource_t;

// ========== 应用配置结构（Flash保存） ==========
#define CONFIG_MAGIC 0x5AA55AA5
#define CONFIG_VER   0x0001

typedef struct {
	uint32_t magic;              // 魔数
	uint16_t ver;                // 版本
	uint16_t len;                // 长度
	
	// 阈值参数
	uint16_t light_low_warn;     // 光照预警下限
	uint16_t light_high_warn;    // 光照预警上限
	uint16_t light_low_alarm;    // 光照报警下限
	uint16_t light_high_alarm;   // 光照报警上限
	
	uint16_t temp_high_warn;     // 温度预警上限
	uint16_t temp_high_alarm;    // 温度报警上限
	
	uint16_t humi_low_warn;      // 湿度预警下限
	uint16_t humi_high_warn;     // 湿度预警上限
	uint16_t humi_low_alarm;     // 湿度报警下限
	uint16_t humi_high_alarm;    // 湿度报警上限
	
	uint16_t gas_high_warn;      // 气体预警上限
	uint16_t gas_high_alarm;     // 气体报警上限
	
	uint16_t upload_period_ms;   // 上传周期
	
	// 功能开关
	uint8_t  default_mode;       // 默认模式
	uint8_t  buzzer_enable;      // 蜂鸣器使能
	uint8_t  reserved[6];        // 保留
	
	uint16_t crc16;              // CRC校验
} AppConfig_t;

// ========== 事件类型 ==========
typedef enum {
	EVT_BOOT = 0,
	EVT_WARN_ENTER,
	EVT_ALARM_ENTER,
	EVT_LEVEL_AUTO_UP,
	EVT_LEVEL_MANUAL_UP,
	EVT_LEVEL_MANUAL_DOWN,
	EVT_RECOVER,
	EVT_SENSOR_FAULT,
	EVT_CFG_SAVE,
	EVT_WDG_RESET,
	EVT_MODE_SWITCH
} EventId_t;

// ========== 事件日志 ==========
typedef struct {
	uint32_t ts_s;           // 时间戳(秒)
	uint8_t  evt;            // 事件ID
	uint8_t  level;          // 报警级别
	uint8_t  src_bitmap;     // 源位图
	uint8_t  state;          // 系统状态
	uint16_t light_mv;       // 光照值
	uint16_t gas_mv;         // 气体值
	uint8_t  temp_c;         // 温度
	uint8_t  humi_rh;        // 湿度
} EventLog_t;

// ========== 统计信息 ==========
typedef struct {
	uint16_t alarm_total;    // 报警总次数
	uint16_t warn_total;     // 预警总次数
	uint16_t fault_total;    // 故障总次数
	uint8_t  max_level_ever; // 历史最高级别
	uint16_t light_max;      // 光照最大值
	uint16_t light_min;      // 光照最小值
	uint32_t run_time_s;     // 运行时间(秒)
} AppStats_t;

// ========== Pro模式运行时数据 ==========
typedef struct {
	uint16_t light_mv;
	uint16_t gas_mv;
	uint8_t  temp_c;
	uint8_t  humi_rh;
	
	WorkMode_t mode;
	SysState_Pro_t sys_state;
	AlarmLevel_t alarm_level;
	
	AlarmSource_t src;
	uint8_t risk_score;
	
	uint16_t no_key_alarm_ms;
	uint8_t  key_event;
	uint8_t  alarm_stable_cnt;
	uint8_t  view_page;
	
	uint8_t  dht_fail_cnt;   // DHT11失败计数
	uint8_t  adc_zero_cnt;   // ADC零值计数
} EnvRuntime_t;

#endif
