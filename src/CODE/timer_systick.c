#include "timer_systick.h"

// 时间节拍标志
volatile uint8_t g_flag_10ms = 0;
volatile uint8_t g_flag_100ms = 0;
volatile uint8_t g_flag_500ms = 0;
volatile uint8_t g_flag_2000ms = 0;

// 内部计数器（0.5ms 单位）
static volatile uint32_t cnt_0_5ms = 0;

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);



static uint32_t Clock_GetAPB1Prescaler(void)
{
    uint32_t ppre1 = (RCC->CFGR >> 8) & 0x07;
    if (ppre1 < 4) return 1;
    return 1UL << (ppre1 - 3);
}

static uint32_t Clock_GetTIM2Clock(void)
{
    uint32_t hclk = SystemCoreClock;
    uint32_t apb1_prescaler = Clock_GetAPB1Prescaler();
    uint32_t pclk1 = hclk / apb1_prescaler;
    return (apb1_prescaler == 1) ? pclk1 : (pclk1 * 2U);
}

/**
 * @brief 初始化 TIM2 作为系统节拍定时器
 * @note  配置为 0.5ms 中断周期，生成 10ms/100ms/500ms/2000ms 时间标志
 */
void TIM2_SysTick_Init(void)
{
    // 1. 使能 TIM2 时钟（APB1）
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // 2. 配置定时器参数：按实际系统时钟生成 2kHz 中断（0.5ms）
    {
        uint32_t tim_clk;
        uint32_t psc;
        SystemCoreClockUpdate();
        tim_clk = Clock_GetTIM2Clock();
        psc = tim_clk / 1000000U;
        if (psc == 0) psc = 1;
        TIM2->PSC = (uint16_t)(psc - 1U);      // 先分频到约 1MHz
        TIM2->ARR = 500 - 1;                   // 1MHz / 500 = 2kHz
    }

    // 3. 使能更新中断
    TIM2->DIER |= TIM_DIER_UIE;

    // 4. 配置 NVIC（寄存器方式）
    // TIM2_IRQn = 28，位于 ISER[0]
    NVIC->ISER[0] |= (1 << TIM2_IRQn);
    NVIC->IP[TIM2_IRQn] = 0x40;  // 优先级 1（抢占优先级 1，子优先级 0）

    // 5. 启动定时器
    TIM2->CR1 |= TIM_CR1_CEN;
}

/**
 * @brief TIM2 中断服务函数
 * @note  每 0.5ms 触发一次，生成多级时间标志
 */
void TIM2_IRQHandler(void)
{
    // 检查更新中断标志
    if (TIM2->SR & TIM_SR_UIF) {
        // 清除中断标志
        TIM2->SR &= ~TIM_SR_UIF;

        // 递增计数器
        cnt_0_5ms++;

        // 生成时间标志
        if (cnt_0_5ms % 20 == 0)   g_flag_10ms = 1;    // 10ms = 0.5ms * 20
        if (cnt_0_5ms % 200 == 0)  g_flag_100ms = 1;   // 100ms = 0.5ms * 200
        if (cnt_0_5ms % 1000 == 0) g_flag_500ms = 1;   // 500ms = 0.5ms * 1000
        if (cnt_0_5ms % 4000 == 0) g_flag_2000ms = 1;  // 2000ms = 0.5ms * 4000
    }
}
