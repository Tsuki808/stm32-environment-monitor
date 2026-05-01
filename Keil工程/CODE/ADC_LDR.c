#include "ADC_LDR.h"
#include "stm32f10x.h"
#include "app_eventlog.h"
#include <stdio.h>

#define ADC_CH_NUM     2
#define ADC_SAMPLES    16
#define ADC_BUF_SIZE   (ADC_CH_NUM * ADC_SAMPLES)
#define ADC_CAL_TIMEOUT 1000000UL

// DMA 缓冲
volatile uint16_t adc_dma_buf[ADC_BUF_SIZE];

// 滤波结果
static uint16_t light_filtered = 0;
static uint16_t gas_filtered = 0;

// 初始化 ADC1 规则扫描、TIM3 触发、DMA 循环传输
void ADC_Init_Continuous(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef ADC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	uint32_t timeout;
	
	// 1. 开启时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1 | RCC_APB2Periph_AFIO, ENABLE);
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	
	// 2. ADC 时钟分频 (72MHz / 6 = 12MHz)
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);
	
	// 3. GPIO 配置 (PA5=Light, PA7=Gas)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// 4. DMA1 Channel1 配置
	DMA_DeInit(DMA1_Channel1);
	DMA_ClearFlag(DMA1_FLAG_GL1 | DMA1_FLAG_TC1 | DMA1_FLAG_HT1 | DMA1_FLAG_TE1);
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)adc_dma_buf;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_BufferSize = ADC_BUF_SIZE;
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
	DMA_Init(DMA1_Channel1, &DMA_InitStructure);
	DMA_Cmd(DMA1_Channel1, ENABLE);
	
	// 5. ADC1 配置
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = ENABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; // TIM3 触发
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel = 2;
	ADC_Init(ADC1, &ADC_InitStructure);
	
	// 6. 配置通道和顺序
	ADC_RegularChannelConfig(ADC1, ADC_Channel_5, 1, ADC_SampleTime_55Cycles5);
	ADC_RegularChannelConfig(ADC1, ADC_Channel_7, 2, ADC_SampleTime_55Cycles5);
	
	// 7. 使能 ADC DMA 和 ADC
	ADC_DMACmd(ADC1, ENABLE);
	ADC_Cmd(ADC1, ENABLE);
	
	// 8. ADC 校准
	ADC_ResetCalibration(ADC1);
	timeout = ADC_CAL_TIMEOUT;
	while(ADC_GetResetCalibrationStatus(ADC1) && timeout) timeout--;
	if(timeout == 0) {
		AppProtocol_SendPayload("@ERR,src=ADC,code=RESET_CAL_TIMEOUT");
	}
	ADC_StartCalibration(ADC1);
	timeout = ADC_CAL_TIMEOUT;
	while(ADC_GetCalibrationStatus(ADC1) && timeout) timeout--;
	if(timeout == 0) {
		AppProtocol_SendPayload("@ERR,src=ADC,code=CAL_TIMEOUT");
	}
	
	// 9. 使能外部触发
	ADC_ExternalTrigConvCmd(ADC1, ENABLE);
	
	// 10. TIM3 配置 (100Hz = 10ms 周期)
	// 72MHz / 7200 = 10kHz, 10000 / 100 = 100Hz
	TIM_TimeBaseStructure.TIM_Period = 100 - 1;
	TIM_TimeBaseStructure.TIM_Prescaler = 7200 - 1;
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
	
	// TIM3 TRGO 输出用于触发 ADC
	TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
	TIM_Cmd(TIM3, ENABLE);
}

// 简单冒泡排序，用于中值滤波
static void bubble_sort(uint16_t *arr, uint8_t n) {
	for(uint8_t i = 0; i < n - 1; i++) {
		for(uint8_t j = 0; j < n - i - 1; j++) {
			if(arr[j] > arr[j + 1]) {
				uint16_t tmp = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = tmp;
			}
		}
	}
}

// 取 DMA 数据进行滤波处理
void ADC_Update_Filtered(uint16_t *light_mv_out, uint16_t *gas_mv_out)
{
	uint16_t light_buf[ADC_SAMPLES];
	uint16_t gas_buf[ADC_SAMPLES];
	uint32_t light_sum = 0;
	uint32_t gas_sum = 0;
	uint8_t i;
	
	if(DMA_GetFlagStatus(DMA1_FLAG_TC1) == RESET) {
		*light_mv_out = light_filtered;
		*gas_mv_out = gas_filtered;
		return;
	}
	DMA_ClearFlag(DMA1_FLAG_TC1);

	DMA_Cmd(DMA1_Channel1, DISABLE);
	DMA_SetCurrDataCounter(DMA1_Channel1, ADC_BUF_SIZE);
	
	// 1. 分离双通道数据 (偶数位置LDR, 奇数位置GAS)
	for(i = 0; i < ADC_SAMPLES; i++) {
		light_buf[i] = adc_dma_buf[i * 2];
		gas_buf[i]   = adc_dma_buf[i * 2 + 1];
	}
	DMA_Cmd(DMA1_Channel1, ENABLE);
	
	// 2. 排序 (中值滤波准备)
	bubble_sort(light_buf, ADC_SAMPLES);
	bubble_sort(gas_buf, ADC_SAMPLES);
	
	// 3. 去掉极值，取中间 8 个求平均
	for(i = 4; i < 12; i++) {
		light_sum += light_buf[i];
		gas_sum += gas_buf[i];
	}
	uint16_t light_avg = light_sum / 8;
	uint16_t gas_avg = gas_sum / 8;
	
	// 4. 转换为电压 (mV)
	uint16_t light_mv_new = (light_avg * 3300) / 4095;
	uint16_t gas_mv_new   = (gas_avg * 3300) / 4095;
	
	// 5. 软件 IIR 滤波 (平滑)
	if(light_filtered == 0) light_filtered = light_mv_new;
	else light_filtered = (light_filtered * 3 + light_mv_new) / 4;
	
	if(gas_filtered == 0) gas_filtered = gas_mv_new;
	else gas_filtered = (gas_filtered * 3 + gas_mv_new) / 4;
	
	// 6. 输出结果
	*light_mv_out = light_filtered;
	*gas_mv_out   = gas_filtered;
}

// 向下兼容接口，仅返回最新的滤波值
void ADC_Sample_Light(uint16_t *light_mv)
{
	*light_mv = light_filtered;
}

void ADC_Sample_Gas(uint16_t *gas_mv)
{
	*gas_mv = gas_filtered;
}
