#ifndef _DHT11_H
#define _DHT11_H

#include <stdio.h>
#include "stm32f10x.h"

// DHT11时序读取会短暂关闭TIM2中断，是阻塞式读取。
uint8_t Read_DHT11_Data_Blocking(uint8_t *temp_c, uint8_t *humi_rh);

// 兼容旧接口名，新的业务代码不要再调用这个名字。
uint8_t Read_DHT11_Data_NonBlock(uint8_t *temp_c, uint8_t *humi_rh);

#endif
