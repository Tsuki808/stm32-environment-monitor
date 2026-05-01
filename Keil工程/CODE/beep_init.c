#include "beep_init.h"
#include "stm32f10x.h"
#include "board_config.h"

void beep_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(BEEP_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = BEEP_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(BEEP_GPIO_PORT, &GPIO_InitStructure);
	
	GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
}
