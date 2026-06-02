#ifndef _USART_GROUNDSTATION_H
#define _USART_GROUNDSTATION_H

#include "stm32f10x.h"

// 初始化地面站 USART1（中断接收模式）
void GroundStation_USART_Init(void);

// 轮询命令（在主循环中调用）
void GroundStation_PollCommand(void);

// 获取统计信息
uint16_t GroundStation_GetDroppedCount(void);
uint16_t GroundStation_GetOverflowCount(void);

#endif
