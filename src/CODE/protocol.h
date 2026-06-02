#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include "stm32f10x.h"

// 处理接收到的命令
void Protocol_ProcessCommand(char *cmd);

// 发送环境数据帧（每 500ms 调用）
void Protocol_SendEnvFrame(uint16_t adc_mv, uint8_t level, uint8_t state, uint8_t risk_score);

// 发送事件帧
void Protocol_SendEventFrame(const char *event_msg);

// 发送响应帧
void Protocol_SendResponseFrame(const char *key, const char *value);

// 发送带 XOR 校验的任意载荷帧，payload 必须以 @ 开头且不含 *CS
void Protocol_SendPayload(const char *payload);

#endif
