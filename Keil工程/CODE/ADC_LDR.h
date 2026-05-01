#ifndef _ADC_LDR_H
#define _ADC_LDR_H

#include "stm32f10x.h"

// ADC连续扫描与DMA初始化
void ADC_Init_Continuous(void);

// 获取滤波后的ADC数据 (多通道)
void ADC_Update_Filtered(uint16_t *light_mv_out, uint16_t *gas_mv_out);

// 向下兼容接口
void ADC_Sample_Light(uint16_t *light_mv);
void ADC_Sample_Gas(uint16_t *gas_mv);

#endif
