#include "usart_groundstation.h"
#include <string.h>

// 外部协议处理函数（由 protocol.c 实现）
extern void Protocol_ProcessCommand(char *cmd);

#define UART_CMD_BUF_SIZE 128

// 命令接收缓冲区
static volatile uint8_t uart_cmd_ready = 0;
static char uart_pending_cmd[UART_CMD_BUF_SIZE];
static volatile uint16_t uart_cmd_dropped = 0;
static volatile uint16_t uart_rx_overflow = 0;

/**
 * @brief 初始化地面站 USART1（中断接收模式）
 * @note  GPIO 已在 qimo 的 GPIO_Init_All() 中配置（PA9=TX, PA10=RX）
 *        此函数只配置 USART1 外设和中断
 */
void GroundStation_USART_Init(void)
{
    // 1. 使能 USART1 时钟
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // 2. 波特率：HSI = 8MHz, APB2 = 1, PCLK2 = 8MHz
    //    USARTDIV = 8000000 / (16 * 115200) = 4.340...
    //    BRR mantissa=4, fraction=5 → BRR = (4<<4)|5 = 0x45 = 69
    USART1->BRR = 69;  // 8MHz / 69 ≈ 115942 bps (误差 0.64%, UART 容限 ±2.5%)

    // 3. 使能 USART1 + 接收中断
    USART1->CR1 = USART_CR1_TE |       // 发送使能
                  USART_CR1_RE |       // 接收使能
                  USART_CR1_RXNEIE |   // 接收中断使能
                  USART_CR1_UE;        // USART 使能

    // 4. 配置 NVIC（寄存器方式）
    // USART1_IRQn = 37，位于 ISER[1]（37 - 32 = 5）
    NVIC->ISER[1] |= (1 << (USART1_IRQn - 32));
    NVIC->IP[USART1_IRQn] = 0xC0;  // 优先级 3（抢占优先级 3，子优先级 0）
}

/**
 * @brief 轮询命令（在主循环中调用）
 * @note  从中断缓冲区取出完整命令并调用协议处理函数
 */
void GroundStation_PollCommand(void)
{
    char cmd[UART_CMD_BUF_SIZE];

    // 临界区保护：禁用接收中断
    USART1->CR1 &= ~USART_CR1_RXNEIE;

    if (!uart_cmd_ready) {
        // 没有待处理命令，恢复中断并返回
        USART1->CR1 |= USART_CR1_RXNEIE;
        return;
    }

    // 复制命令到本地缓冲区
    strncpy(cmd, uart_pending_cmd, UART_CMD_BUF_SIZE - 1);
    cmd[UART_CMD_BUF_SIZE - 1] = '\0';
    uart_cmd_ready = 0;

    // 恢复中断
    USART1->CR1 |= USART_CR1_RXNEIE;

    // 调用协议处理函数
    Protocol_ProcessCommand(cmd);
}

/**
 * @brief 获取丢弃的命令数量
 */
uint16_t GroundStation_GetDroppedCount(void)
{
    return uart_cmd_dropped;
}

/**
 * @brief 获取溢出次数
 */
uint16_t GroundStation_GetOverflowCount(void)
{
    return uart_rx_overflow;
}

/**
 * @brief USART1 中断服务函数
 * @note  接收字符并组装成完整命令（以 \r 或 \n 结尾）
 */
void USART1_IRQHandler(void)
{
    static char cmd_buf[UART_CMD_BUF_SIZE];
    static uint8_t cmd_len = 0;

    // 检查接收中断标志
    if (USART1->SR & USART_SR_RXNE) {
        // 读取接收到的字符
        char ch = (char)(USART1->DR & 0xFF);

        // 检查是否为命令结束符
        if (ch == '\r' || ch == '\n') {
            if (cmd_len > 0) {
                // 命令接收完成
                cmd_buf[cmd_len] = '\0';

                if (!uart_cmd_ready) {
                    // 缓冲区空闲，复制命令
                    strncpy(uart_pending_cmd, cmd_buf, UART_CMD_BUF_SIZE - 1);
                    uart_pending_cmd[UART_CMD_BUF_SIZE - 1] = '\0';
                    uart_cmd_ready = 1;
                } else {
                    // 缓冲区忙，丢弃命令
                    uart_cmd_dropped++;
                }

                cmd_len = 0;
            }
        } else if (cmd_len < UART_CMD_BUF_SIZE - 1) {
            // 累积字符
            cmd_buf[cmd_len++] = ch;
        } else {
            // 缓冲区溢出，重置
            uart_rx_overflow++;
            cmd_len = 0;
        }
    }
}
