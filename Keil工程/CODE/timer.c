#include "timer.h"
#include "stm32f10x.h"
#include "sys.h"
#include "app_types.h"
#include "app_types_pro.h"
#include "app_config.h"
#include "board_config.h"

// 外部变量声明
extern volatile uint8_t g_flag_10ms;
extern volatile uint8_t g_flag_100ms;
extern volatile uint8_t g_flag_500ms;
extern volatile uint8_t g_flag_2000ms;
extern AppData_t g_app;
extern EnvRuntime_t g_rt;

// 数码管扫描相关
extern uint8_t disp_buf[4];
extern const uint8_t NUM[10];
static uint8_t scan_pos = 0;

// 蜂鸣器控制相关
static uint16_t beep_timer = 0;
static uint8_t beep_state = 0;

// LED闪烁控制
static uint16_t led_timer = 0;

static const uint16_t display_seg_pins[8] = {
	DISPLAY_SEG_A_PIN, DISPLAY_SEG_B_PIN, DISPLAY_SEG_C_PIN, DISPLAY_SEG_D_PIN,
	DISPLAY_SEG_E_PIN, DISPLAY_SEG_F_PIN, DISPLAY_SEG_G_PIN, DISPLAY_SEG_DP_PIN
};

static const uint16_t display_digit_pins[4] = {
	DISPLAY_DIG1_PIN, DISPLAY_DIG2_PIN, DISPLAY_DIG3_PIN, DISPLAY_DIG4_PIN
};

// TIM2系统节拍初始化 - 0.5ms周期，适配F103C6低密度器件。
void TIM2_SystemTick_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	// 配置TIM2: 72MHz / 36 / 1000 = 2000Hz (0.5ms)
	// F103: APB1=36MHz, 定时器时钟自动倍频到72MHz
	TIM_TimeBaseStructure.TIM_Period = 1000 - 1;      // 计数周期
	TIM_TimeBaseStructure.TIM_Prescaler = 36 - 1;     // 预分频
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

	// 配置NVIC
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	// 使能更新中断
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	
	// 使能定时器
	TIM_Cmd(TIM2, ENABLE);
}

// TIM2中断服务函数 - 每0.5ms执行一次
void TIM2_IRQHandler(void)
{
	static uint16_t cnt_0_5ms = 0;
	
	if(TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		cnt_0_5ms++;
		
		// 1. 蜂鸣器方波生成
		Beep_WaveControl();
		
		// 2. 每1ms扫描一位数码管
		if(cnt_0_5ms % 2 == 0) {
			Display_ScanNext();
		}
		
		// 3. LED闪烁控制
		LED_FlashControl();
		
		// 4. 生成软件节拍标志
		if(cnt_0_5ms % 20 == 0) {  // 10ms
			g_flag_10ms = 1;
		}
		
		if(cnt_0_5ms % 200 == 0) {  // 100ms
			g_flag_100ms = 1;
		}
		
		if(cnt_0_5ms % 1000 == 0) {  // 500ms
			g_flag_500ms = 1;
		}
		
		if(cnt_0_5ms % 2000 == 0) {  // 1000ms (1秒)
			extern void App_IncTimestamp(void);
			App_IncTimestamp();  // 时间戳递增
		}
		
		if(cnt_0_5ms % 4000 == 0) {  // 2000ms
			g_flag_2000ms = 1;
			cnt_0_5ms = 0;  // 复位计数器
		}
		
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}

// 数码管扫描函数
void Display_ScanNext(void)
{
	uint16_t seg_data;
	
	uint8_t i;
	GPIO_ResetBits(DISPLAY_GPIO_PORT, DISPLAY_DIG_PINS);
	
	// 输出段码
	seg_data = NUM[disp_buf[scan_pos]];
	
	for(i = 0; i < 8; i++) {
		if(seg_data & (1 << i)) GPIO_SetBits(DISPLAY_GPIO_PORT, display_seg_pins[i]);
		else GPIO_ResetBits(DISPLAY_GPIO_PORT, display_seg_pins[i]);
	}
	
	GPIO_SetBits(DISPLAY_GPIO_PORT, display_digit_pins[scan_pos]);
	
	// 切换到下一位
	scan_pos = (scan_pos + 1) % 4;
}

// 蜂鸣器方波控制
void Beep_WaveControl(void)
{
	if(g_config.buzzer_enable == 0 || g_app.sys_state == 0) {  // 正常或静音
		GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
		beep_timer = 0;
		beep_state = 0;
		return;
	}
	
	// 根据报警级别设置节拍
	uint16_t on_time = 0, off_time = 0;
	
	if(g_rt.sys_state == SYS_FAULT || g_rt.sys_state == SYS_FAULT_LOCK) {
		on_time = (g_rt.sys_state == SYS_FAULT_LOCK) ? 200 : 100;
		off_time = (g_rt.sys_state == SYS_FAULT_LOCK) ? 200 : 1900;
	} else {
	
		switch(g_app.alarm_level) {
			case 1:  // L1: 100ms响, 900ms停
				on_time = 200;   // 100ms / 0.5ms
				off_time = 1800; // 900ms / 0.5ms
				break;
			case 2:  // L2: 200ms响, 300ms停
				on_time = 400;
				off_time = 600;
				break;
			case 3:  // L3: 100ms响, 100ms停
				on_time = 200;
				off_time = 200;
				break;
			default:
				GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
				return;
		}
	}
	
	beep_timer++;
	
	if(beep_state == 0) {  // 发声阶段
		if(GPIO_ReadOutputDataBit(BEEP_GPIO_PORT, BEEP_GPIO_PIN)) {
			GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
		} else {
			GPIO_SetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
		}
		if(beep_timer >= on_time) {
			beep_timer = 0;
			beep_state = 1;
			GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
		}
	} else {  // 静音阶段
		GPIO_ResetBits(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
		if(beep_timer >= off_time) {
			beep_timer = 0;
			beep_state = 0;
		}
	}
}

// LED闪烁控制
void LED_FlashControl(void)
{
	led_timer++;
	
	if(g_app.sys_state == 0) {  // 正常状态
		GPIO_SetBits(LED_GPIO_PORT, LED_NORMAL_PIN);
		GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN | LED_FAULT_PIN);
		led_timer = 0;
		return;
	}
	
	// 报警状态
	GPIO_ResetBits(LED_GPIO_PORT, LED_NORMAL_PIN);
	
	if(g_rt.sys_state == SYS_FAULT || g_rt.sys_state == SYS_FAULT_LOCK) {
		if(led_timer >= ((g_rt.sys_state == SYS_FAULT_LOCK) ? 200 : 1000)) {
			if(GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_ALARM_PIN)) {
				GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN | LED_FAULT_PIN);
			} else {
				GPIO_SetBits(LED_GPIO_PORT, LED_ALARM_PIN | LED_FAULT_PIN);
			}
			led_timer = 0;
		}
		return;
	}
	
	switch(g_app.alarm_level) {
		case 1:  // L1: LED2 1Hz闪烁
			if(led_timer >= 1000) {  // 500ms / 0.5ms
				if(GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_ALARM_PIN)) {
					GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN);
				} else {
					GPIO_SetBits(LED_GPIO_PORT, LED_ALARM_PIN);
				}
				GPIO_ResetBits(LED_GPIO_PORT, LED_FAULT_PIN);
				led_timer = 0;
			}
			break;
			
		case 2:  // L2: LED2 2Hz闪烁
			if(led_timer >= 500) {  // 250ms / 0.5ms
				if(GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_ALARM_PIN)) {
					GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN);
				} else {
					GPIO_SetBits(LED_GPIO_PORT, LED_ALARM_PIN);
				}
				GPIO_ResetBits(LED_GPIO_PORT, LED_FAULT_PIN);
				led_timer = 0;
			}
			break;
			
		case 3:  // L3: LED3 5Hz快闪
			if(led_timer >= 200) {  // 100ms / 0.5ms
				GPIO_ResetBits(LED_GPIO_PORT, LED_ALARM_PIN);
				if(GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_FAULT_PIN)) {
					GPIO_ResetBits(LED_GPIO_PORT, LED_FAULT_PIN);
				} else {
					GPIO_SetBits(LED_GPIO_PORT, LED_FAULT_PIN);
				}
				led_timer = 0;
			}
			break;
	}
}
