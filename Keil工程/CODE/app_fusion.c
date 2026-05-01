#include "app_fusion.h"
#include "app_config.h"
#include "app_eventlog.h"
#include <stdio.h>

// 初始化融合模块
void AppFusion_Init(void)
{
	// 预留初始化代码
}

// 更新报警源位图
void AppFusion_UpdateAlarmSource(EnvRuntime_t *rt, const AppConfig_t *cfg)
{
	// 光照异常判断
	if(rt->src.adc_fault) {
		rt->src.light_abn = 0;
	} else if(rt->light_mv < cfg->light_low_alarm || rt->light_mv > cfg->light_high_alarm) {
		rt->src.light_abn = 1;
	} else if(rt->light_mv >= cfg->light_low_warn && rt->light_mv <= cfg->light_high_warn) {
		rt->src.light_abn = 0;
	}
	
	// 温度异常判断
	if(rt->src.dht_fault) {
		rt->src.temp_abn = 0;
	} else if(rt->temp_c > cfg->temp_high_alarm) {
		rt->src.temp_abn = 1;
	} else if(rt->temp_c <= cfg->temp_high_warn) {
		rt->src.temp_abn = 0;
	}
	
	// 湿度异常判断
	if(rt->src.dht_fault) {
		rt->src.humi_abn = 0;
	} else if(rt->humi_rh < cfg->humi_low_alarm || rt->humi_rh > cfg->humi_high_alarm) {
		rt->src.humi_abn = 1;
	} else if(rt->humi_rh >= cfg->humi_low_warn && rt->humi_rh <= cfg->humi_high_warn) {
		rt->src.humi_abn = 0;
	}
	
	// 气体异常判断
	if(rt->src.adc_fault) {
		rt->src.gas_abn = 0;
	} else if(rt->gas_mv > cfg->gas_high_alarm) {
		rt->src.gas_abn = 1;
	} else if(rt->gas_mv <= cfg->gas_high_warn) {
		rt->src.gas_abn = 0;
	}
}

// 计算风险评分
uint8_t AppFusion_CalcRiskScore(const EnvRuntime_t *rt, const AppConfig_t *cfg)
{
	uint8_t score = 0;
	
	if(rt->mode == MODE_BASIC) {
		// Basic模式：只看光照
		if(rt->src.light_abn) {
			score = WEIGHT_LIGHT;
		}
	} else {
		// Pro模式：多源融合
		if(rt->src.light_abn) score += WEIGHT_LIGHT;
		if(rt->src.temp_abn)  score += WEIGHT_TEMP;
		if(rt->src.humi_abn)  score += WEIGHT_HUMI;
		if(rt->src.gas_abn)   score += WEIGHT_GAS;
	}
	
	return score;
}

// 获取自动升级时间（根据异常源数量）
uint16_t AppFusion_GetAutoUpgradeTime(const EnvRuntime_t *rt)
{
	(void)rt;
	return 2000;
}

// 多源融合处理
void AppFusion_Process(EnvRuntime_t *rt, const AppConfig_t *cfg)
{
	uint16_t upgrade_time;
	
	// 更新报警源
	AppFusion_UpdateAlarmSource(rt, cfg);
	
	// 计算风险评分
	rt->risk_score = AppFusion_CalcRiskScore(rt, cfg);
	
	// 状态转换逻辑
	if(rt->risk_score == 0) {
		// 正常状态
		if(rt->sys_state == SYS_WARN || rt->sys_state == SYS_ALARM) {
			rt->alarm_stable_cnt++;
			if(rt->alarm_stable_cnt >= 5) {  // 500ms稳定
				rt->sys_state = SYS_NORMAL;
				rt->alarm_level = ALARM_L0;
				rt->alarm_stable_cnt = 0;
				AppProtocol_SendPayload("@EVT,msg=RECOVER");
			}
		} else {
			rt->alarm_stable_cnt = 0;
		}
	} else if(rt->risk_score >= RISK_ALARM_THRESHOLD) {
		// 报警状态
		rt->alarm_stable_cnt = 0;
		upgrade_time = AppFusion_GetAutoUpgradeTime(rt);
		if(rt->sys_state != SYS_ALARM) {
			rt->sys_state = SYS_ALARM;
			
			rt->alarm_level = ALARM_L1;
			
			rt->no_key_alarm_ms = 0;
			AppProtocol_SendPayload("@EVT,msg=ALARM_ENTER,level=1");
		}
		
		// 自动升级逻辑
		if(upgrade_time > 0 && rt->no_key_alarm_ms >= upgrade_time && rt->alarm_level < ALARM_L3) {
			rt->alarm_level++;
			rt->no_key_alarm_ms = 0;
			{
				char payload[48];
				snprintf(payload, sizeof(payload), "@EVT,msg=LEVEL_AUTO_UP,level=%d", rt->alarm_level);
				AppProtocol_SendPayload(payload);
			}
		}
		
	} else if(rt->risk_score >= RISK_WARN_THRESHOLD) {
		// 预警状态
		rt->alarm_stable_cnt = 0;
		if(rt->sys_state == SYS_NORMAL) {
			rt->sys_state = SYS_WARN;
			AppProtocol_SendPayload("@EVT,msg=WARN_ENTER");
		}
	}
	
	// 故障检测
	if(rt->dht_fail_cnt >= 3) {
		uint8_t new_fault = (rt->src.dht_fault == 0);
		rt->src.dht_fault = 1;
		rt->src.temp_abn = 0;
		rt->src.humi_abn = 0;
		if(rt->mode == MODE_PRO && rt->sys_state != SYS_ALARM && rt->sys_state != SYS_FAULT_LOCK) {
			rt->sys_state = SYS_FAULT;
			if(new_fault) AppProtocol_SendPayload("@ERR,src=DHT,code=TIMEOUT");
		}
	} else {
		rt->src.dht_fault = 0;
		if(rt->sys_state == SYS_FAULT) {
			rt->sys_state = SYS_NORMAL;
			rt->alarm_level = ALARM_L0;
		}
	}
	
	if(rt->adc_zero_cnt >= 20) {  // 2秒全0
		uint8_t new_fault = (rt->src.adc_fault == 0);
		rt->src.adc_fault = 1;
		rt->src.light_abn = 0;
		rt->src.gas_abn = 0;
		rt->sys_state = SYS_FAULT_LOCK;
		rt->alarm_level = ALARM_L3;
		if(new_fault) AppProtocol_SendPayload("@ERR,src=ADC,code=FAULT");
	} else {
		rt->src.adc_fault = 0;
	}
}
