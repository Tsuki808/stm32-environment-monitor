#include "app_diag.h"
#include "app_eventlog.h"
#include "delay.h"
#include "led.h"
#include "beep_init.h"
#include "display.h"
#include "dht11.h"
#include "ADC_LDR.h"
#include "board_config.h"
#include <stdio.h>

static uint8_t wdg_reset_seen = 0;

// IWDG 看门狗初始化
// 超时时间: ~3000ms，覆盖2s DHT11阻塞采样周期并保留余量。
void AppDiag_IWDG_Init(void)
{
	// 开启看门狗
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	// IWDG_LSI为40KHz，256分频后为156.25Hz (6.4ms/tick)
	IWDG_SetPrescaler(IWDG_Prescaler_256);
	// 3000ms / 6.4ms ~= 469 ticks
	IWDG_SetReload(469);
	IWDG_ReloadCounter();
	IWDG_Enable();
}

// 喂狗
void AppDiag_IWDG_Feed(void)
{
	IWDG_ReloadCounter();
}

uint8_t AppDiag_WasWatchdogReset(void)
{
	return wdg_reset_seen;
}

// 传感器数据冻结检测 (1秒执行1次)
void AppDiag_CheckFrozen(EnvRuntime_t *rt)
{
	static uint16_t last_light = 0;
	static uint16_t last_gas = 0;
	static uint8_t frozen_cnt = 0;
	
	if(rt->light_mv == last_light && rt->gas_mv == last_gas) {
		frozen_cnt++;
		if(frozen_cnt >= 5) { // 5秒不变
		AppProtocol_SendPayload("@ERR,src=SENSOR,code=STUCK");
			// 标记故障，但不锁定系统，降级继续运行
			frozen_cnt = 0; 
		}
	} else {
		frozen_cnt = 0;
		last_light = rt->light_mv;
		last_gas = rt->gas_mv;
	}
}

// 上电自检
uint8_t AppDiag_PowerOnSelfTest(void)
{
	uint8_t dht_temp, dht_humi;
	uint16_t light, gas;
	uint8_t pass = 1;
	
	// 检查看门狗复位标志
	if(RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) {
		AppProtocol_SendPayload("@EVT,msg=WDG_RESET");
		wdg_reset_seen = 1;
		RCC_ClearFlag();
	}
	
	// 1. LED 三灯轮检
	GPIO_SetBits(LED_GPIO_PORT, LED_NORMAL_PIN); delay_ms(100); GPIO_ResetBits(LED_GPIO_PORT, LED_NORMAL_PIN);
	GPIO_SetBits(LED_GPIO_PORT, LED_ALARM_PIN); delay_ms(100); GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN);
	GPIO_SetBits(LED_GPIO_PORT, LED_FAULT_PIN); delay_ms(100); GPIO_ResetBits(LED_GPIO_PORT, LED_FAULT_PIN);
	
	// 2. 蜂鸣器短鸣两声
	GPIO_SetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN); delay_ms(100); GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN); delay_ms(100);
	GPIO_SetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN); delay_ms(100); GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN); delay_ms(100);
	
	// 3. 数码管8888测试 (此处简化)
	disp_buf[0] = 8;
	disp_buf[1] = 8;
	disp_buf[2] = 8;
	disp_buf[3] = 8;
	delay_ms(500);
	
	// 4. ADC初步测试
	ADC_Update_Filtered(&light, &gas);
	if((light == 0 || light >= 3290) && (gas == 0 || gas >= 3290)) {
		AppProtocol_SendPayload("@ERR,src=ADC,code=FAULT");
		pass = 0;
	}
	
	// 5. DHT11 测试
	if(Read_DHT11_Data_Blocking(&dht_temp, &dht_humi) != 0) {
		AppProtocol_SendPayload("@ERR,src=DHT,code=TIMEOUT");
		pass = 0;
	}
	
	if(pass) {
		AppProtocol_SendPayload("@SELFTEST,result=PASS");
	} else {
		AppProtocol_SendPayload("@SELFTEST,result=FAIL");
	}
	
	return pass;
}
