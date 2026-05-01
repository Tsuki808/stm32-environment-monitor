#include "led.h"
#include "stm32f10x.h"
#include "board_config.h"

// LED初始化函数 - STM32F103风格
void LED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(LED_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = LED_NORMAL_PIN | LED_ALARM_PIN | LED_FAULT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // F103推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(LED_GPIO_PORT, &GPIO_InitStructure);
	
	// 初始状态: LED1亮(正常指示), LED2/LED3灭
	GPIO_SetBits(LED_GPIO_PORT, LED_NORMAL_PIN);
	GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN | LED_FAULT_PIN);
}
