#ifndef _BOARD_CONFIG_H
#define _BOARD_CONFIG_H

#include "stm32f10x.h"

/*
 * STM32F103C6T6/LQFP48 pin map.
 * F103C6 does not expose the old high-pin-count C/D port pin map.
 * Keep PA13/PA14 for SWD; disable only JTAG so PB3/PB4 can be GPIO.
 */

#define BOARD_AFIO_CLOCK        RCC_APB2Periph_AFIO
#define BOARD_DISABLE_JTAG()    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)

#define LED_GPIO_CLK            RCC_APB2Periph_GPIOA
#define LED_GPIO_PORT           GPIOA
#define LED_NORMAL_PIN          GPIO_Pin_0
#define LED_ALARM_PIN           GPIO_Pin_1
#define LED_FAULT_PIN           GPIO_Pin_2

#define BEEP_GPIO_CLK           RCC_APB2Periph_GPIOA
#define BEEP_GPIO_PORT          GPIOA
#define BEEP_GPIO_PIN           GPIO_Pin_3

#define DHT11_GPIO_CLK          RCC_APB2Periph_GPIOA
#define DHT11_GPIO_PORT         GPIOA
#define DHT11_GPIO_PIN          GPIO_Pin_4

#define KEY_GPIO_CLK            RCC_APB2Periph_GPIOB
#define KEY_GPIO_PORT           GPIOB
#define KEY1_GPIO_PIN           GPIO_Pin_0
#define KEY2_GPIO_PIN           GPIO_Pin_1
#define KEY3_GPIO_PIN           GPIO_Pin_2
#define KEY4_GPIO_PIN           GPIO_Pin_3

#define DISPLAY_GPIO_CLK        RCC_APB2Periph_GPIOB
#define DISPLAY_GPIO_PORT       GPIOB
#define DISPLAY_SEG_A_PIN       GPIO_Pin_4
#define DISPLAY_SEG_B_PIN       GPIO_Pin_5
#define DISPLAY_SEG_C_PIN       GPIO_Pin_6
#define DISPLAY_SEG_D_PIN       GPIO_Pin_7
#define DISPLAY_SEG_E_PIN       GPIO_Pin_8
#define DISPLAY_SEG_F_PIN       GPIO_Pin_9
#define DISPLAY_SEG_G_PIN       GPIO_Pin_10
#define DISPLAY_SEG_DP_PIN      GPIO_Pin_11
#define DISPLAY_DIG1_PIN        GPIO_Pin_12
#define DISPLAY_DIG2_PIN        GPIO_Pin_13
#define DISPLAY_DIG3_PIN        GPIO_Pin_14
#define DISPLAY_DIG4_PIN        GPIO_Pin_15

#define DISPLAY_SEG_PINS        (DISPLAY_SEG_A_PIN | DISPLAY_SEG_B_PIN | DISPLAY_SEG_C_PIN | DISPLAY_SEG_D_PIN | \
                                 DISPLAY_SEG_E_PIN | DISPLAY_SEG_F_PIN | DISPLAY_SEG_G_PIN | DISPLAY_SEG_DP_PIN)
#define DISPLAY_DIG_PINS        (DISPLAY_DIG1_PIN | DISPLAY_DIG2_PIN | DISPLAY_DIG3_PIN | DISPLAY_DIG4_PIN)
#define DISPLAY_ALL_PINS        (DISPLAY_SEG_PINS | DISPLAY_DIG_PINS)

#define SR04_GPIO_CLK           RCC_APB2Periph_GPIOA
#define SR04_GPIO_PORT          GPIOA
#define SR04_TRIG_PIN           GPIO_Pin_6
#define SR04_ECHO_PIN           GPIO_Pin_8

#endif
