#ifndef _KEY_H
#define _KEY_H

#include "stm32f10x.h"

void KEY_Init(void);
void EXTILine_Config(void);
void Key_Scan10ms(void);

#endif
