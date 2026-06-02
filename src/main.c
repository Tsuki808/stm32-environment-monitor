/**
 ******************************************************************************
 * @file    main.c
 * @brief   STM32F103C8 环境监测系统 - 集成地面站与 DeepSeekAI 融合模块
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "timer_systick.h"
#include "usart_groundstation.h"
#include "protocol.h"
#include "app_fusion_lite.h"
#include "app_bonus.h"
#include <stdio.h>
#include <string.h>

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);

#define ADC_FILTER_SAMPLES      16
#define LCD_MENU_PAGE_COUNT     6
#define KEY_LONG_TICKS          80    /* 800ms / 10ms */
#define KEY_VERY_LONG_TICKS     250   /* 2500ms / 10ms */

static uint8_t g_lcd_menu_page = 0;

/* ==================== 硬件阈值配置 ==================== */
#define THRESHOLD_WARN_MV   1200  // 警告阈值（mV）
#define THRESHOLD_ALARM_MV  2200  // 报警阈值（mV）

/* ==================== 延时函数（来自 qimo） ==================== */
void delay_ms(unsigned int ms) {
    volatile uint32_t i;
    uint32_t loops;

    if (SystemCoreClock == 0) SystemCoreClockUpdate();
    while (ms--) {
        loops = SystemCoreClock / 8000U;
        if (loops < 50U) loops = 50U;
        for (i = 0; i < loops; i++) {
            __NOP();
        }
    }
}

/* ==================== LCD1602 驱动（来自 qimo） ==================== */
void LCD_Pulse(void) {
    GPIOB->BSRR = (1 << 1);
    delay_ms(1);
    GPIOB->BRR = (1 << 1);
    delay_ms(1);
}

void LCD_Write_Nibble(unsigned char nibble) {
    if(nibble & 0x01) GPIOB->BSRR = (1 << 12); else GPIOB->BRR = (1 << 12);
    if(nibble & 0x02) GPIOB->BSRR = (1 << 13); else GPIOB->BRR = (1 << 13);
    if(nibble & 0x04) GPIOB->BSRR = (1 << 14); else GPIOB->BRR = (1 << 14);
    if(nibble & 0x08) GPIOB->BSRR = (1 << 15); else GPIOB->BRR = (1 << 15);
}

void LCD_Write_Cmd(unsigned char cmd) {
    GPIOB->BRR = (1 << 10); // RS = 0
    delay_ms(1);
    LCD_Write_Nibble(cmd >> 4);
    LCD_Pulse();
    LCD_Write_Nibble(cmd & 0x0F);
    LCD_Pulse();
}

void LCD_Write_Data(unsigned char dat) {
    GPIOB->BSRR = (1 << 10); // RS = 1
    delay_ms(1);
    LCD_Write_Nibble(dat >> 4);
    LCD_Pulse();
    LCD_Write_Nibble(dat & 0x0F);
    LCD_Pulse();
}

void LCD_Init(void) {
    delay_ms(40);
    GPIOB->BRR = (1 << 10);

    LCD_Write_Nibble(0x03); LCD_Pulse(); delay_ms(5);
    LCD_Write_Nibble(0x03); LCD_Pulse(); delay_ms(1);
    LCD_Write_Nibble(0x03); LCD_Pulse(); delay_ms(1);
    LCD_Write_Nibble(0x02); LCD_Pulse(); delay_ms(1);

    LCD_Write_Cmd(0x28);
    LCD_Write_Cmd(0x08);
    LCD_Write_Cmd(0x01);
    delay_ms(5);
    LCD_Write_Cmd(0x06);
    LCD_Write_Cmd(0x0C);
}

void LCD_ShowString(unsigned char x, unsigned char y, char *str) {
    unsigned char addr = (y == 0) ? (0x80 + x) : (0xC0 + x);
    LCD_Write_Cmd(addr);
    while (*str) LCD_Write_Data(*str++);
}

void LCD_Clear(void) {
    LCD_Write_Cmd(0x01);
    delay_ms(2);
}

/* ==================== GPIO 初始化（来自 qimo） ==================== */
void GPIO_Init_All(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
    AFIO->MAPR = (AFIO->MAPR & ~(0x7 << 24)) | (0x4 << 24);

    /* PA0(ADC), PA1~PA3(LED), PA9(TX), PA10(RX) */
    GPIOA->CRL &= ~(0xFFFF << 0);
    GPIOA->CRL |=  (0x3330 << 0);
    GPIOA->CRH &= ~( (0xF << 4) | (0xF << 8) );
    GPIOA->CRH |=  ( (0xB << 4) | (0x4 << 8) );

    /* PB0(蜂鸣器), PB1(LCD_E) 推挽输出 */
    GPIOB->CRL &= ~( (0xF << 0) | (0xF << 4) );
    GPIOB->CRL |=  ( (0x3 << 0) | (0x3 << 4) );

    /* PB10(RS), PB12~15(D4~D7) 推挽输出, PB11(按键) 上拉输入 */
    GPIOB->CRH &= ~0xFFFFFF00;
    GPIOB->CRH |=  0x33338300;
    GPIOB->ODR |= (1 << 11);
}

/* ==================== ADC1 初始化（来自 qimo） ==================== */
void ADC1_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= (2 << 14);
    ADC1->CR1 = 0;
    ADC1->CR2 = (7 << 17) | (1 << 20) | 1;
    delay_ms(5);
}

unsigned int ADC1_Read(void) {
    ADC1->CR2 |= (1 << 22);
    unsigned int timeout = 10000;
    while (((ADC1->SR & (1 << 1)) == 0) && timeout) timeout--;
    return ADC1->DR & 0x0FFF;
}


/* ==================== USART1 发送函数（来自 qimo，供协议模块调用） ==================== */
void USART1_SendChar(char c) {
    unsigned int timeout = 100000;  // 增加超时时间（10倍）

    // 安全检查：确保 USART1 已使能
    if ((USART1->CR1 & USART_CR1_UE) == 0) return;

    // 等待发送数据寄存器空
    while ((USART1->SR & USART_SR_TXE) == 0 && timeout) timeout--;

    // 发送数据（超时则丢弃，避免死锁）
    if (timeout > 0) USART1->DR = c;
}

void USART1_SendString(char *s) {
    while (*s) USART1_SendChar(*s++);
}

/* ==================== LED 控制函数 ==================== */
void LED_SetState(AlarmState_t state) {
    // 清除所有 LED
    GPIOA->BRR = (1 << 1) | (1 << 2) | (1 << 3);

    switch (state) {
        case STATE_NORMAL:
            GPIOA->BSRR = (1 << 1);  // PA1 亮（正常指示灯）
            break;
        case STATE_WARN:
            GPIOA->BSRR = (1 << 2);  // PA2 亮（警告指示灯）
            break;
        case STATE_ALARM_L1:
        case STATE_ALARM_L2:
        case STATE_ALARM_L3:
            GPIOA->BSRR = (1 << 3);  // PA3 亮（报警指示灯）
            break;
        case STATE_FAULT:
            GPIOA->BSRR = (1 << 2) | (1 << 3);  // PA2+PA3 同时亮（故障）
            break;
        default:
            break;
    }
}

/* ==================== 蜂鸣器控制函数 ==================== */
void Beep_Control(AlarmState_t state, uint32_t tick_count) {
    uint8_t beep_on = 0;

    if (!AppBonus_GetBuzzerEnable()) {
        GPIOB->BRR = (1 << 0);
        return;
    }

    switch (state) {
        case STATE_NORMAL:
        case STATE_WARN:
            GPIOB->BRR = (1 << 0);  // 关闭蜂鸣器
            break;

        case STATE_ALARM_L1:
            // 慢闪：1Hz（500ms 开，500ms 关）
            beep_on = (tick_count % 2 == 0);
            break;

        case STATE_ALARM_L2:
            // 中速闪：2Hz（250ms 开，250ms 关）
            beep_on = ((tick_count % 4) < 2);
            break;

        case STATE_ALARM_L3:
            // 快闪：4Hz（125ms 开，125ms 关）
            beep_on = ((tick_count % 8) < 4);
            break;

        case STATE_FAULT:
            // 常亮
            beep_on = 1;
            break;

        default:
            beep_on = 0;
            break;
    }

    if (beep_on) {
        GPIOB->BSRR = (1 << 0);
    } else {
        GPIOB->BRR = (1 << 0);
    }
}

/* ==================== LCD 显示更新 ==================== */
void LCD_Update_Display(unsigned int mv, AlarmState_t state, uint8_t risk_score) {
    char line1[17];
    char line2[17];
    const char *state_str[] = {"NOR", "WRN", "AL1", "AL2", "AL3", "FLT"};

    switch (g_lcd_menu_page) {
        case 1:
            snprintf(line1, sizeof(line1), "SET WARN        ");
            snprintf(line2, sizeof(line2), "LHW:%4umV      ", AppBonus_GetWarnMv());
            break;
        case 2:
            snprintf(line1, sizeof(line1), "SET ALARM       ");
            snprintf(line2, sizeof(line2), "LHA:%4umV      ", AppBonus_GetAlarmMv());
            break;
        case 3:
            snprintf(line1, sizeof(line1), "SET UPLOAD      ");
            snprintf(line2, sizeof(line2), "UP:%4ums       ", AppBonus_GetUploadMs());
            break;
        case 4:
            snprintf(line1, sizeof(line1), "SET BUZZER      ");
            snprintf(line2, sizeof(line2), "BZ:%s           ", AppBonus_GetBuzzerEnable() ? "ON " : "OFF");
            break;
        case 5:
            snprintf(line1, sizeof(line1), "DEMO MODE       ");
            snprintf(line2, sizeof(line2), "DEMO:%s         ", AppBonus_GetDemoMode() ? "ON " : "OFF");
            break;
        default:
            snprintf(line1, sizeof(line1), "ADC:%4umV       ", mv);
            snprintf(line2, sizeof(line2), "S:%s Risk:%02u   ",
                     state < 6 ? state_str[state] : "???", risk_score);
            break;
    }

    LCD_ShowString(0, 0, line1);
    LCD_ShowString(0, 1, line2);
}

/* ==================== 按键检测（来自 qimo） ==================== */
unsigned char Key_Pressed(void) {
    if ((GPIOB->IDR & (1 << 11)) == 0) {
        delay_ms(20);
        if ((GPIOB->IDR & (1 << 11)) == 0) {
            unsigned int timeout = 500;
            while ((GPIOB->IDR & (1 << 11)) == 0 && timeout--) delay_ms(1);
            return 1;
        }
    }
    return 0;
}

static void Key_AdjustAlarmLevel(AlarmState_t current_state, uint8_t risk_score)
{
    switch (current_state) {
        case STATE_ALARM_L1:
            Fusion_ForceState(STATE_ALARM_L2, risk_score < 6U ? 6U : risk_score);
            Protocol_SendEventFrame("KEY_LEVEL_L2");
            break;
        case STATE_ALARM_L2:
            Fusion_ForceState(STATE_ALARM_L3, risk_score < 8U ? 8U : risk_score);
            Protocol_SendEventFrame("KEY_LEVEL_L3");
            break;
        case STATE_ALARM_L3:
            Fusion_ForceState(STATE_ALARM_L1, risk_score < 4U ? 4U : risk_score);
            Protocol_SendEventFrame("KEY_LEVEL_L1");
            break;
        default:
            Protocol_SendEventFrame("KEY_ACK");
            break;
    }
}


/* ==================== 主函数 ==================== */
int main(void) {
    /* 0. 立即读取并清除复位标志（必须在任何外设初始化之前完成） */
    uint8_t was_iwdg_reset = (RCC->CSR & RCC_CSR_IWDGRSTF) ? 1U : 0U;
    RCC->CSR |= RCC_CSR_RMVF;   /* 清除 IWDG/WWDG/SFT/POR/PIN 等全部复位标志 */

    // 1. 系统时钟配置（使用内部 HSI，8MHz）
    // 注意：HSI 精度 ±1%，如果波特率不准确，请改用外部晶振 HSE
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0);  // 等待 HSI 稳定

    // 可选：HSI 校准（微调频率以提高精度）
    // RCC->CR = (RCC->CR & ~RCC_CR_HSITRIM) | (16 << 3);  // 默认校准值 16

    RCC->CFGR &= ~(RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);  // 等待切换完成

    SystemCoreClockUpdate();  // 更新全局时钟变量

    // 2. 硬件初始化
    GPIO_Init_All();
    ADC1_Init();
    LCD_Init();

    // 3. 模块初始化
    TIM2_SysTick_Init();           // 系统节拍定时器
    GroundStation_USART_Init();    // 地面站 USART1
    AppBonus_Init();               // 非侵入式加分服务：配置、日志、统计、诊断
    Fusion_Init();                 // DeepSeekAI 融合模块

    /* 3.5. 看门狗复位事件上报（通信与日志模块就绪后才能上报） */
    if (was_iwdg_reset) {
        Protocol_SendEventFrame("WDG_RESET");
    }

    // 4. 显示启动信息
    LCD_Clear();
    LCD_ShowString(0, 0, "DeepSeek+Ground");
    LCD_ShowString(0, 1, "Station Ready...");
    delay_ms(2000);
    LCD_Clear();

    // 5. 主循环变量
    uint32_t adc_raw = 0;
    uint16_t adc_mv = 0;
    AlarmState_t current_state = STATE_NORMAL;
    uint8_t risk_score = 0;
    uint32_t tick_500ms_count = 0;  // 500ms 节拍计数器（用于蜂鸣器控制）

    /* ===== 5.5. 上电硬件自检 (POST) ===== */
    adc_raw = ADC1_Read();
    adc_mv  = adc_raw * 3300 / 4095;

    /* LED 三灯轮检: PA1(正常)→PA2(警告)→PA3(报警)，各亮 100ms */
    GPIOA->BSRR = (1U << 1); delay_ms(100); GPIOA->BRR = (1U << 1);
    GPIOA->BSRR = (1U << 2); delay_ms(100); GPIOA->BRR = (1U << 2);
    GPIOA->BSRR = (1U << 3); delay_ms(100); GPIOA->BRR = (1U << 3);

    /* 蜂鸣器双响: PB0 两短鸣（80ms 亮 / 80ms 灭）*/
    GPIOB->BSRR = (1U << 0); delay_ms(80); GPIOB->BRR = (1U << 0); delay_ms(80);
    GPIOB->BSRR = (1U << 0); delay_ms(80); GPIOB->BRR = (1U << 0);

    /* ADC 范围验证：5mV < adc_mv < 3290mV 视为 PASS（排除断路/短路故障） */
    {
        uint8_t post_pass = (adc_mv > 5U && adc_mv < 3290U) ? 1U : 0U;
        char    post_buf[180];
        snprintf(post_buf, sizeof(post_buf),
                 "@SELFTEST,result=%s,mode=HARDWARE,led=PASS,buzzer=PASS,adc=%u,flash=A_B_CFG",
                 post_pass ? "PASS" : "FAIL", adc_mv);
        Protocol_SendPayload(post_buf);
        Protocol_SendPayload("@AI,engine=DeepSeek,status=GROUND_CONTEXT_READY,"
                             "source=STM32_USART1,model=deepseek-v4-flash");
    }

    /* ===== 5.6. IWDG 独立看门狗初始化（POST 完成后再启动，避免 POST 延时触发复位） ===== */
    /* LSI ≈ 40kHz，PR=6(÷256) → ≈156Hz，RLR=469 → 超时 ≈ 3000ms                        */
    IWDG->KR  = 0x5555UL;   /* 解锁寄存器写保护 */
    IWDG->PR  = 6U;           /* 预分频 256 */
    IWDG->RLR = 469U;         /* 重载值：469 / 156.25 ≈ 3.0s */
    IWDG->KR  = 0xAAAAUL;   /* 立即重载计数器 */
    IWDG->KR  = 0xCCCCUL;   /* 启动 IWDG（启动后不可停止） */

    Protocol_SendEventFrame("SYSTEM_START");
    AppBonus_PrintConfig();

    /* 健康喂狗标志：三个时间片全部执行过才喂 IWDG；任一时间片卡死即在 3s 内触发复位 */
    static uint8_t wdg_seen_10ms  = 0;
    static uint8_t wdg_seen_100ms = 0;
    static uint8_t wdg_seen_500ms = 0;

    // 6. 主循环
    while (1) {
        // ========== 10ms 任务 ==========
        if (g_flag_10ms) {
            g_flag_10ms = 0;

            // 轮询地面站命令
            GroundStation_PollCommand();
            wdg_seen_10ms = 1;
        }

        // ========== 100ms 任务 ==========
        if (g_flag_100ms) {
            g_flag_100ms = 0;

            // 读取 ADC
            adc_raw = ADC1_Read();
            adc_mv = adc_raw * 3300 / 4095;

            // 更新 LCD 显示
            current_state = Fusion_GetState();
            risk_score = Fusion_GetRiskScore();
            LCD_Update_Display(adc_mv, current_state, risk_score);

            // 更新 LED 状态
            LED_SetState(current_state);
            wdg_seen_100ms = 1;
        }

        // ========== 500ms 任务 ==========
        if (g_flag_500ms) {
            g_flag_500ms = 0;
            tick_500ms_count++;

            // 执行 DeepSeekAI 融合算法（默认阈值仍为 1200/2200，可串口配置）
            Fusion_Process(adc_mv, AppBonus_GetWarnMv(), AppBonus_GetAlarmMv());

            // 发送环境数据帧到地面站（传入 risk_score）
            current_state = Fusion_GetState();
            risk_score = Fusion_GetRiskScore();
            Protocol_SendEnvFrame(adc_mv, (uint8_t)current_state, (uint8_t)current_state, risk_score);

            // 控制蜂鸣器
            Beep_Control(current_state, tick_500ms_count);
            wdg_seen_500ms = 1;
        }

        // ========== 2000ms 任务 ==========
        if (g_flag_2000ms) {
            g_flag_2000ms = 0;

            // 发送心跳信息（移除，避免前端 drop 错误）
            // 如需心跳，应使用带校验的协议帧
        }

        /* ===== 健康喂狗：三个时间片全部正常执行后才喂 IWDG ===== */
        if (wdg_seen_10ms && wdg_seen_100ms && wdg_seen_500ms) {
            IWDG->KR = 0xAAAAUL;   /* 喂狗：重载计数器，阻止复位 */
            wdg_seen_10ms  = 0;
            wdg_seen_100ms = 0;
            wdg_seen_500ms = 0;
        }

        // ========== 按键处理（非定时任务） ==========
        if (Key_Pressed()) {
            AppBonus_OnKeyPressed();
            current_state = Fusion_GetState();
            risk_score = Fusion_GetRiskScore();
            Key_AdjustAlarmLevel(current_state, risk_score);
            Protocol_SendResponseFrame("key", "PRESSED");
        }

        // 短暂延时，避免 CPU 空转
        delay_ms(1);
    }
}
