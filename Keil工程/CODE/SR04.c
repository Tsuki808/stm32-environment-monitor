#include "SR04.h"
#include "stm32f10x.h"
#include "board_config.h"
#include "app_eventlog.h"
#include <stdio.h>

// 注意：本项目不使用SR04模块
// 此文件保留仅为兼容性，实际不调用

void HCSR04_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(SR04_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = SR04_TRIG_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // F103推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SR04_GPIO_PORT, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = SR04_ECHO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;  // F103浮空输入
	GPIO_Init(SR04_GPIO_PORT, &GPIO_InitStructure);
}

uint32_t SR04_Getdis(void)
{
	uint32_t dis;
	uint32_t tmp = 0;
	uint32_t timeout = 5000;

	// 超声波模块初始化
	HCSR04_Init();
	
	GPIO_SetBits(SR04_GPIO_PORT, SR04_TRIG_PIN);
	delay_us(10);
	GPIO_ResetBits(SR04_GPIO_PORT, SR04_TRIG_PIN);
	
	// 等待回响，保留超时以避免兼容代码被误调用时卡死。
	while(GPIO_ReadInputDataBit(SR04_GPIO_PORT, SR04_ECHO_PIN) == 0)
	{
		delay_us(8);
		if(timeout-- == 0) return 0;
	}
	
	while(GPIO_ReadInputDataBit(SR04_GPIO_PORT, SR04_ECHO_PIN) == 1)
	{
		delay_us(8);
		tmp++;
		if(tmp > 5000) break;  // 超时保护
	}

	dis = tmp * 3 / 2;
	
	return dis;
}

void GETdis(void)
{
	uint32_t dis;
	char payload[40];
	
	dis = SR04_Getdis();
	snprintf(payload, sizeof(payload), "@SR04,dist=%lu", (unsigned long)dis);
	AppProtocol_SendPayload(payload);
}
