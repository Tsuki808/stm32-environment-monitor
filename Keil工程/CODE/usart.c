#include "usart.h"
#include "stm32f10x.h"
#include <string.h>

extern void UART_ProcessCommand(char *cmd);

#define UART_CMD_BUF_SIZE 128

static volatile uint8_t uart_cmd_ready = 0;
static char uart_pending_cmd[UART_CMD_BUF_SIZE];
static volatile uint16_t uart_cmd_dropped = 0;
static volatile uint16_t uart_rx_overflow = 0;

void USART_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	// 开启GPIOA和USART1时钟 - F103使用APB2
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	
	// 配置PA9为复用推挽输出(USART1 TX) - F103风格
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 复用推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// 配置PA10为浮空输入(USART1 RX) - F103风格
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;  // 浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// USART1配置
	USART_InitStructure.USART_BaudRate = 115200;  // 波特率115200
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);
	
	// 配置USART1中断(可选，用于接收)
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	// 使能接收中断(可选)
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	
	// 使能USART1
	USART_Cmd(USART1, ENABLE);
}

void USART_PollCommand(void)
{
	char cmd[UART_CMD_BUF_SIZE];

	USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
	if(!uart_cmd_ready) {
		USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
		return;
	}
	strncpy(cmd, uart_pending_cmd, UART_CMD_BUF_SIZE - 1);
	cmd[UART_CMD_BUF_SIZE - 1] = '\0';
	uart_cmd_ready = 0;
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	UART_ProcessCommand(cmd);
}

uint16_t USART_GetDroppedCommandCount(void)
{
	return uart_cmd_dropped;
}

uint16_t USART_GetOverflowCount(void)
{
	return uart_rx_overflow;
}

// USART1中断服务函数(可选)
void USART1_IRQHandler(void)
{
	uint16_t data;
	static char cmd_buf[UART_CMD_BUF_SIZE];
	static uint8_t cmd_len = 0;
	char ch;
	
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
	{
		data = USART_ReceiveData(USART1);
		ch = (char)(data & 0xFF);
		
		if(ch == '\r' || ch == '\n') {
			if(cmd_len > 0) {
				cmd_buf[cmd_len] = '\0';
				if(!uart_cmd_ready) {
					strncpy(uart_pending_cmd, cmd_buf, UART_CMD_BUF_SIZE - 1);
					uart_pending_cmd[UART_CMD_BUF_SIZE - 1] = '\0';
					uart_cmd_ready = 1;
				} else {
					uart_cmd_dropped++;
				}
				cmd_len = 0;
			}
		} else if(cmd_len < UART_CMD_BUF_SIZE - 1) {
			cmd_buf[cmd_len++] = ch;
		} else {
			uart_rx_overflow++;
			cmd_len = 0;
		}
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
