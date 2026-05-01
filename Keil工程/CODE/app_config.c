#include "app_config.h"
#include "stm32f10x.h"
#include <string.h>

AppConfig_t g_config;

static uint32_t AppConfig_GetSeq(const AppConfig_t *cfg)
{
	uint32_t seq = 0;
	memcpy(&seq, cfg->reserved, sizeof(seq));
	return seq;
}

static void AppConfig_SetSeq(AppConfig_t *cfg, uint32_t seq)
{
	memcpy(cfg->reserved, &seq, sizeof(seq));
}

static uint8_t AppConfig_UserFieldsEqual(const AppConfig_t *a, const AppConfig_t *b)
{
	return a->light_low_warn == b->light_low_warn &&
	       a->light_high_warn == b->light_high_warn &&
	       a->light_low_alarm == b->light_low_alarm &&
	       a->light_high_alarm == b->light_high_alarm &&
	       a->temp_high_warn == b->temp_high_warn &&
	       a->temp_high_alarm == b->temp_high_alarm &&
	       a->humi_low_warn == b->humi_low_warn &&
	       a->humi_high_warn == b->humi_high_warn &&
	       a->humi_low_alarm == b->humi_low_alarm &&
	       a->humi_high_alarm == b->humi_high_alarm &&
	       a->gas_high_warn == b->gas_high_warn &&
	       a->gas_high_alarm == b->gas_high_alarm &&
	       a->upload_period_ms == b->upload_period_ms &&
	       a->default_mode == b->default_mode &&
	       a->buzzer_enable == b->buzzer_enable;
}

static uint8_t AppConfig_ReadPage(uint32_t addr, AppConfig_t *cfg)
{
	AppConfig_t *flash_cfg = (AppConfig_t*)addr;
	uint16_t crc_calc;

	if(flash_cfg->magic != CONFIG_MAGIC) return 0;
	if(flash_cfg->ver != CONFIG_VER) return 0;
	if(flash_cfg->len != sizeof(AppConfig_t)) return 0;

	memcpy(cfg, flash_cfg, sizeof(AppConfig_t));
	crc_calc = AppConfig_CalcCRC16((uint8_t*)cfg, sizeof(AppConfig_t) - 2);
	return (crc_calc == cfg->crc16);
}

static uint8_t AppConfig_WritePage(uint32_t addr, const AppConfig_t *cfg)
{
	AppConfig_t cfg_to_save;
	uint16_t *data = (uint16_t*)&cfg_to_save;
	uint16_t len = sizeof(AppConfig_t) / 2;
	uint16_t i;

	memcpy(&cfg_to_save, cfg, sizeof(AppConfig_t));
	cfg_to_save.magic = CONFIG_MAGIC;
	cfg_to_save.ver = CONFIG_VER;
	cfg_to_save.len = sizeof(AppConfig_t);
	cfg_to_save.crc16 = AppConfig_CalcCRC16((uint8_t*)&cfg_to_save, sizeof(AppConfig_t) - 2);

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	if(FLASH_ErasePage(addr) != FLASH_COMPLETE) {
		FLASH_Lock();
		return 0;
	}

	for(i = 0; i < len; i++) {
		if(FLASH_ProgramHalfWord(addr, data[i]) != FLASH_COMPLETE) {
			FLASH_Lock();
			return 0;
		}
		addr += 2;
	}
	FLASH_Lock();

	return 1;
}

// CRC16计算（CCITT）
uint16_t AppConfig_CalcCRC16(const uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFF;
	uint16_t i, j;
	
	for(i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for(j = 0; j < 8; j++) {
			if(crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc = crc << 1;
		}
	}
	
	return crc;
}

// 设置默认配置
void AppConfig_SetDefault(AppConfig_t *cfg)
{
	cfg->magic = CONFIG_MAGIC;
	cfg->ver = CONFIG_VER;
	cfg->len = sizeof(AppConfig_t);
	
	cfg->light_low_warn = DEFAULT_LIGHT_LOW_WARN;
	cfg->light_high_warn = DEFAULT_LIGHT_HIGH_WARN;
	cfg->light_low_alarm = DEFAULT_LIGHT_LOW_ALARM;
	cfg->light_high_alarm = DEFAULT_LIGHT_HIGH_ALARM;
	
	cfg->temp_high_warn = DEFAULT_TEMP_HIGH_WARN;
	cfg->temp_high_alarm = DEFAULT_TEMP_HIGH_ALARM;
	
	cfg->humi_low_warn = DEFAULT_HUMI_LOW_WARN;
	cfg->humi_high_warn = DEFAULT_HUMI_HIGH_WARN;
	cfg->humi_low_alarm = DEFAULT_HUMI_LOW_ALARM;
	cfg->humi_high_alarm = DEFAULT_HUMI_HIGH_ALARM;
	
	cfg->gas_high_warn = DEFAULT_GAS_HIGH_WARN;
	cfg->gas_high_alarm = DEFAULT_GAS_HIGH_ALARM;
	
	cfg->upload_period_ms = DEFAULT_UPLOAD_PERIOD;
	
	cfg->default_mode = DEFAULT_MODE;
	cfg->buzzer_enable = DEFAULT_BUZZER_ENABLE;
	
	memset(cfg->reserved, 0, sizeof(cfg->reserved));
	AppConfig_SetSeq(cfg, 0);
	
	// 计算CRC（不包括CRC字段本身）
	cfg->crc16 = AppConfig_CalcCRC16((uint8_t*)cfg, sizeof(AppConfig_t) - 2);
}

// 从Flash加载配置
uint8_t AppConfig_Load(AppConfig_t *cfg)
{
	AppConfig_t cfg_a;
	AppConfig_t cfg_b;
	uint8_t valid_a = AppConfig_ReadPage(APP_CFG_FLASH_ADDR_A, &cfg_a);
	uint8_t valid_b = AppConfig_ReadPage(APP_CFG_FLASH_ADDR_B, &cfg_b);

	if(valid_a && valid_b) {
		*cfg = (AppConfig_GetSeq(&cfg_b) > AppConfig_GetSeq(&cfg_a)) ? cfg_b : cfg_a;
		return 1;
	}
	if(valid_a) {
		*cfg = cfg_a;
		return 1;
	}
	if(valid_b) {
		*cfg = cfg_b;
		return 1;
	}
	return 0;
}

// 保存配置到Flash
uint8_t AppConfig_Save(const AppConfig_t *cfg)
{
	uint32_t addr;
	AppConfig_t cfg_to_save;
	AppConfig_t cfg_a;
	AppConfig_t cfg_b;
	uint8_t valid_a = AppConfig_ReadPage(APP_CFG_FLASH_ADDR_A, &cfg_a);
	uint8_t valid_b = AppConfig_ReadPage(APP_CFG_FLASH_ADDR_B, &cfg_b);
	uint32_t seq_a = valid_a ? AppConfig_GetSeq(&cfg_a) : 0;
	uint32_t seq_b = valid_b ? AppConfig_GetSeq(&cfg_b) : 0;
	uint32_t next_seq = ((seq_a > seq_b) ? seq_a : seq_b) + 1;
	
	memcpy(&cfg_to_save, cfg, sizeof(AppConfig_t));
	cfg_to_save.magic = CONFIG_MAGIC;
	cfg_to_save.ver = CONFIG_VER;
	cfg_to_save.len = sizeof(AppConfig_t);
	AppConfig_SetSeq(&cfg_to_save, next_seq);
	cfg_to_save.crc16 = AppConfig_CalcCRC16((uint8_t*)&cfg_to_save, sizeof(AppConfig_t) - 2);

	if(valid_a && seq_a >= seq_b && AppConfig_UserFieldsEqual(&cfg_a, &cfg_to_save)) return 1;
	if(valid_b && seq_b > seq_a && AppConfig_UserFieldsEqual(&cfg_b, &cfg_to_save)) return 1;

	addr = (seq_a <= seq_b) ? APP_CFG_FLASH_ADDR_A : APP_CFG_FLASH_ADDR_B;
	if(!AppConfig_WritePage(addr, &cfg_to_save)) {
		return 0;
	}
	return AppConfig_ReadPage(addr, &cfg_to_save);
}

// 初始化配置
void AppConfig_Init(void)
{
	if(!AppConfig_Load(&g_config)) {
		// 加载失败，使用默认配置
		AppConfig_SetDefault(&g_config);
	}
}
