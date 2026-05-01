#ifndef _DISPLAY_H
#define _DISPLAY_H

#include "stm32f10x.h"
#include "app_types.h"

// 全局变量声明
extern const uint8_t NUM[10];
extern uint8_t disp_buf[4];

// 函数声明
void display_Init(void);
void Display_UpdateBuffer(AppData_t *app);

#endif
