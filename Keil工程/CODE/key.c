#include "key.h"
#include "stm32f10x.h"
#include "app_types.h"
#include "board_config.h"

extern AppData_t g_app;

#define KEY_COUNT       4
#define KEY_LONG_TICKS  200  // 2s / 10ms

static uint8_t key_stable[KEY_COUNT];
static uint8_t key_last_raw[KEY_COUNT] = {1, 1, 1, 1};
static uint8_t key_debounce[KEY_COUNT];
static uint16_t key_press_ticks[KEY_COUNT];
static uint8_t key_long_sent[KEY_COUNT];
static uint8_t combo_long_sent;

static uint8_t Key_ReadRaw(uint8_t index)
{
	switch(index) {
		case 0: return GPIO_ReadInputDataBit(KEY_GPIO_PORT, KEY1_GPIO_PIN);
		case 1: return GPIO_ReadInputDataBit(KEY_GPIO_PORT, KEY2_GPIO_PIN);
		case 2: return GPIO_ReadInputDataBit(KEY_GPIO_PORT, KEY3_GPIO_PIN);
		case 3: return GPIO_ReadInputDataBit(KEY_GPIO_PORT, KEY4_GPIO_PIN);
		default: return 1;
	}
}

void KEY_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	uint8_t i;
	
	RCC_APB2PeriphClockCmd(KEY_GPIO_CLK | BOARD_AFIO_CLOCK, ENABLE);
	BOARD_DISABLE_JTAG();
	GPIO_InitStructure.GPIO_Pin = KEY1_GPIO_PIN | KEY2_GPIO_PIN | KEY3_GPIO_PIN | KEY4_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(KEY_GPIO_PORT, &GPIO_InitStructure);
	
	for(i = 0; i < KEY_COUNT; i++) {
		key_stable[i] = 1;
		key_last_raw[i] = 1;
		key_debounce[i] = 0;
		key_press_ticks[i] = 0;
		key_long_sent[i] = 0;
	}
	combo_long_sent = 0;
}

void EXTILine_Config(void)
{
	KEY_Init();
}

void Key_Scan10ms(void)
{
	uint8_t i;
	uint8_t raw;
	
	for(i = 0; i < KEY_COUNT; i++) {
		raw = Key_ReadRaw(i);
		if(raw == key_last_raw[i]) {
			if(key_debounce[i] < 2) key_debounce[i]++;
		} else {
			key_debounce[i] = 0;
			key_last_raw[i] = raw;
		}
		
		if(key_debounce[i] >= 2 && raw != key_stable[i]) {
			key_stable[i] = raw;
			if(raw == 0) {
				key_press_ticks[i] = 0;
				key_long_sent[i] = 0;
			} else {
				if(!key_long_sent[i] && !combo_long_sent && g_app.key_event == KEY_EVT_NONE) {
					g_app.key_event = KEY_EVT_K1_SHORT + i;
				}
				key_press_ticks[i] = 0;
				key_long_sent[i] = 0;
				if(key_stable[0] && key_stable[3]) combo_long_sent = 0;
			}
		}
		
		if(key_stable[i] == 0 && key_press_ticks[i] < 0xFFFF) {
			key_press_ticks[i]++;
		}
	}
	
	if(key_stable[0] == 0 && key_stable[3] == 0) {
		if(!combo_long_sent && key_press_ticks[0] >= KEY_LONG_TICKS && key_press_ticks[3] >= KEY_LONG_TICKS) {
			combo_long_sent = 1;
			key_long_sent[0] = 1;
			key_long_sent[3] = 1;
			if(g_app.key_event == KEY_EVT_NONE) g_app.key_event = KEY_EVT_K1_K4_LONG;
		}
	} else if(key_stable[0] == 0 && !key_long_sent[0] && key_press_ticks[0] >= KEY_LONG_TICKS) {
		key_long_sent[0] = 1;
		if(g_app.key_event == KEY_EVT_NONE) g_app.key_event = KEY_EVT_K1_LONG;
	}
}
