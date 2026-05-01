#include "dht11.h"
#include "stm32f10x.h"
#include "delay.h"
#include "board_config.h"

static uint8_t DHT11_data[5];

static uint8_t DHT11_WaitLevel(uint8_t level, uint16_t timeout_us)
{
	while(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == level) {
		if(timeout_us-- == 0) return 1;
		delay_us(1);
	}
	return 0;
}

void DHT11_IOout(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
	
	GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
	delay_ms(20);
}

void DHT11_IOin(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // F103上拉输入
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
}

void DHT11_Start(void)
{
	// GPIO输出模式
	DHT11_IOout();
	// 拉低至少18ms
	GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
	delay_ms(20);
	// 拉高20-40us
	GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
	delay_us(30);
}

int DHT11_Response(void)
{
	int t1=0, t2=0, t3=0;
	
	// GPIO输入模式
	DHT11_IOin();
	
	// 等待80us低电平响应
	while(1)
	{
		delay_us(10);
		if(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == 0)
			break;
		if(++t1 > 100)
			return -1;
	}
	
	// 等待80us高电平响应
	while(1)
	{
		delay_us(10);
		if(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == 1)
			break;
		if(++t2 > 100)
			return -1;
	}
	
	// 等待数据时序低电平开始
	while(1)
	{
		delay_us(10);
		if(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == 0)
			break;
		if(++t3 > 100)
			return -1;
	}
	
	return 0;
}

uint8_t Read_DHT11_DataByte(uint8_t *data)
{
	uint8_t i, tmpe = 0;
	
	for(i=0; i<8; i++)
	{
		// 等待时序低电平结束
		if(DHT11_WaitLevel(0, 80) != 0) return 1;
		delay_us(30);
		if(GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) == 1)
		{
			// 读取数据1
			tmpe |= 1<<(7-i);
			if(DHT11_WaitLevel(1, 100) != 0) return 1;  // 等待高电平数据时序结束
		}
	}
	
	*data = tmpe;
	return 0;
}

// 阻塞读取DHT11数据；会短暂关闭TIM2中断以保护微秒级时序。
uint8_t Read_DHT11_Data_Blocking(uint8_t *temp_c, uint8_t *humi_rh)
{
	uint8_t ret, i;
	
	// 暂时禁用TIM2中断，避免干扰DHT11时序
	TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);
	
	// 1. 发送起始信号
	DHT11_Start();
	
	// 2. 等待响应
	ret = DHT11_Response();
	
	if(ret == 0)
	{
		// 3. 读取数据(共5字节)
		for(i=0; i<5; i++)
		{
			if(Read_DHT11_DataByte(&DHT11_data[i]) != 0) {
				ret = 1;
				break;
			}
		}
		
		// 4. 校验数据
		if(ret == 0 && DHT11_data[0] + DHT11_data[1] + DHT11_data[2] + DHT11_data[3] == DHT11_data[4])
		{
			*humi_rh = DHT11_data[0];
			*temp_c = DHT11_data[2];
			
			// 恢复输出模式
			DHT11_IOout();
			GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
			
			// 恢复TIM2中断
			TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
			
			return 0;  // 成功
		}
	}
	
	// 6. 恢复输出模式
	DHT11_IOout();
	GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
	
	// 恢复TIM2中断
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	
	return 1;  // 失败
}

uint8_t Read_DHT11_Data_NonBlock(uint8_t *temp_c, uint8_t *humi_rh)
{
	return Read_DHT11_Data_Blocking(temp_c, humi_rh);
}
