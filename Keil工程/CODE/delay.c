#include "delay.h"
#include "stm32f10x.h"

// STM32F103 系统时钟72MHz
void delay_us(uint32_t nus)
{
	SysTick->CTRL = 0;       // 关闭SysTick
	SysTick->LOAD = 72*nus;  // F103: 72MHz, 72个时钟周期=1us
	SysTick->VAL = 0;        // 清空当前值
	SysTick->CTRL = 1;       // 使能SysTick，使用处理器时钟
	while ((SysTick->CTRL & 0x00010000)==0);  // 等待计数到0
	SysTick->CTRL = 0;       // 关闭SysTick
}

void delay_ms(uint32_t nms)
{
	for(; nms>0; nms--)
	{
		delay_us(1000);
	}
}
