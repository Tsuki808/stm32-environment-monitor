#include "display.h"
#include "stm32f10x.h"
#include "board_config.h"

// 数码管段码: 0-9
const uint8_t NUM[10] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};

// 显示缓冲区
uint8_t disp_buf[4] = {0, 0, 0, 0};

void display_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(DISPLAY_GPIO_CLK | BOARD_AFIO_CLOCK, ENABLE);
	BOARD_DISABLE_JTAG();

	GPIO_InitStructure.GPIO_Pin = DISPLAY_ALL_PINS;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DISPLAY_GPIO_PORT, &GPIO_InitStructure);
	
	// 初始化全部关闭
	GPIO_ResetBits(DISPLAY_GPIO_PORT, DISPLAY_ALL_PINS);
}

// 更新显示缓冲区
void Display_UpdateBuffer(AppData_t *app)
{
	uint16_t value = 0;
	
	switch(app->view_page) {
		case 0:  // 显示光照电压(mV)
			value = app->light_mv;
			break;
			
		case 1:  // 显示温度(℃ * 10)
			value = app->temp_c * 10;
			break;
			
		case 2:  // 显示湿度(% * 10)
			value = app->humi_rh * 10;
			break;
			
		case 3:  // 显示气体值
			value = app->gas_mv;
			break;
			
		default:
			value = 0;
			break;
	}
	
	// 分解为4位数字
	disp_buf[0] = (value / 1000) % 10;
	disp_buf[1] = (value / 100) % 10;
	disp_buf[2] = (value / 10) % 10;
	disp_buf[3] = value % 10;
}
