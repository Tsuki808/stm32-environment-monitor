#ifndef _USART_H
#define _USART_H

#include "stm32f10x.h"

void USART_Config(void);
void USART_PollCommand(void);
uint16_t USART_GetDroppedCommandCount(void);
uint16_t USART_GetOverflowCount(void);

#endif
