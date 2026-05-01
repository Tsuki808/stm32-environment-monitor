#ifndef _TIMER_H
#define _TIMER_H

#include "stm32f10x.h"
#include "app_types.h"

// 定时器初始化
void TIM2_SystemTick_Init(void);

// 中断服务函数
void TIM2_IRQHandler(void);

// 内部调用函数
void Display_ScanNext(void);
void Beep_WaveControl(void);
void LED_FlashControl(void);

#endif
