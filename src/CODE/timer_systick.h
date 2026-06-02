#ifndef _TIMER_SYSTICK_H
#define _TIMER_SYSTICK_H

#include "stm32f10x.h"

// 时间节拍标志（由 TIM2 中断生成）
extern volatile uint8_t g_flag_10ms;
extern volatile uint8_t g_flag_100ms;
extern volatile uint8_t g_flag_500ms;
extern volatile uint8_t g_flag_2000ms;

// 初始化 TIM2 系统节拍（0.5ms 中断）
void TIM2_SysTick_Init(void);

#endif
