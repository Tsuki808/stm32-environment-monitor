#ifndef _APP_CONFIG_H
#define _APP_CONFIG_H

#include "stm32f10x.h"
#include "app_types_pro.h"

// STM32F103C6: 32KB Flash, 1KB/page. Reserve the last two pages for A/B config.
#define APP_CFG_FLASH_ADDR_A  0x08007800
#define APP_CFG_FLASH_ADDR_B  0x08007C00
#define APP_CFG_FLASH_ADDR    APP_CFG_FLASH_ADDR_A

// 默认配置值
#define DEFAULT_LIGHT_LOW_WARN    1200
#define DEFAULT_LIGHT_HIGH_WARN   2100
#define DEFAULT_LIGHT_LOW_ALARM   1000
#define DEFAULT_LIGHT_HIGH_ALARM  2300

#define DEFAULT_TEMP_HIGH_WARN    30
#define DEFAULT_TEMP_HIGH_ALARM   35

#define DEFAULT_HUMI_LOW_WARN     35
#define DEFAULT_HUMI_HIGH_WARN    80
#define DEFAULT_HUMI_LOW_ALARM    25
#define DEFAULT_HUMI_HIGH_ALARM   90

#define DEFAULT_GAS_HIGH_WARN     1200
#define DEFAULT_GAS_HIGH_ALARM    1600

#define DEFAULT_UPLOAD_PERIOD     500
#define DEFAULT_MODE              MODE_BASIC
#define DEFAULT_BUZZER_ENABLE     1

// 函数声明
void AppConfig_Init(void);
uint8_t AppConfig_Load(AppConfig_t *cfg);
uint8_t AppConfig_Save(const AppConfig_t *cfg);
void AppConfig_SetDefault(AppConfig_t *cfg);
uint16_t AppConfig_CalcCRC16(const uint8_t *data, uint16_t len);

// 全局配置变量
extern AppConfig_t g_config;

#endif
