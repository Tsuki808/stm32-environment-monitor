# STM32F103C8 Environment Monitor — 环境报警监测系统

> 基于 STM32F103C8 的嵌入式环境报警监测系统，采用单 ADC 通道采集、多级状态机融合判定、自定义串口协议通信，配合 Web 地面站实现实时遥测可视化与 DeepSeek AI 智能分析。

## 系统概览

本项目由 **嵌入式固件** 和 **Web 地面站** 两部分组成。固件运行于 STM32F103C8，通过 PA0 单通道 ADC 采集环境模拟量（光敏分压），经多级状态机判定报警等级后，由 USART1 将遥测数据上报至地面站。地面站以纯前端 + Python 本地服务器的方式提供实时曲线、协议终端、命令控制和 AI 分析等功能。

### 核心特性

- **多级报警状态机** — NORMAL → WARN → ALARM L1/L2/L3 → FAULT，支持自动升降级与手动干预
- **风险评分算法** — 基于 ADC 电压的线性映射，输出 0–10 分风险值
- **自定义串口协议** — 带 XOR 校验的结构化帧格式（`@ENV`、`@EVT`、`@CFG`、`@STAT`、`@LOG`、`@ACK`、`@NACK`、`@ERR`）
- **Flash 双页 A/B 配置存储** — CRC16 完整性校验，断电安全写入，支持参数热修改与持久化
- **IWDG 独立看门狗** — 3 秒超时，健康喂狗策略（三个时间片全部正常执行后才重载计数器）
- **上电硬件自检 (POST)** — LED 三灯轮检、蜂鸣器双响、ADC 范围验证
- **ADC 健康监测** — 自动检测 ZERO_STUCK / FULL_STUCK / FROZEN 三类传感器故障
- **Web 地面站** — 实时 ADC 趋势曲线（Canvas）、协议终端、CSV 导出、DeepSeek AI 分析助手

## 项目结构

```
├── src/                          # STM32 固件源码
│   ├── main.c                    # 主程序：GPIO/ADC/LCD/LED/Buzzer/IWDG/主循环
│   ├── stm32f10x_conf.h          # 标准外设库配置头文件
│   └── CODE/
│       ├── app_fusion_lite.c/h   # 多级报警状态机 & 风险评分融合模块
│       ├── protocol.c/h          # 串口通信协议（帧组装/校验/命令解析）
│       ├── timer_systick.c/h     # TIM2 系统节拍（0.5ms 中断 → 10/100/500/2000ms 标志）
│       ├── usart_groundstation.c/h # USART1 中断接收 & 命令轮询
│       └── app_bonus.c/h         # 配置管理/Flash 存储/统计/事件日志/ADC 诊断/Demo 模式
│
├── ground_station/               # Web 地面站
│   ├── index.html                # 仪表盘页面
│   ├── app.js                    # 前端逻辑：遥测解析、Canvas 曲线、命令交互、AI 分析
│   ├── styles.css                # 暗色主题样式
│   ├── visualizer.py             # Python HTTP 服务器 + DeepSeek API 代理
│   ├── serial_probe.py           # 串口调试探针（命令行工具）
│   ├── requirements.txt          # Python 依赖（pyserial）
│   └── README.md                 # 地面站使用说明
│
├── keil/                         # Keil MDK 工程文件
│   ├── STM32.uvprojx             # 工程配置
│   ├── STM32.uvoptx              # 工程选项
│   └── STM32.uvguix.Administrator
│
├── proteus/                      # Proteus 仿真
│   └── EnvAlarm_STM32F103.pdsprj # 电路仿真工程
│
├── .gitignore
└── README.md
```

## 硬件设计

### 引脚分配

| 引脚 | 功能 | 配置 |
|------|------|------|
| PA0 | ADC1_CH0 模拟输入 | 模拟输入（光敏分压） |
| PA1 | 正常指示灯 (LED1) | 推挽输出 |
| PA2 | 警告指示灯 (LED2) | 推挽输出 |
| PA3 | 报警指示灯 (LED3) | 推挽输出 |
| PA9 | USART1_TX | 复用推挽输出 |
| PA10 | USART1_RX | 浮空输入 |
| PB0 | 蜂鸣器 | 推挽输出 |
| PB1 | LCD1602 E (使能) | 推挽输出 |
| PB10 | LCD1602 RS | 推挽输出 |
| PB11 | 按键 | 上拉输入 |
| PB12–PB15 | LCD1602 D4–D7 | 推挽输出 |

### 外设配置

- **系统时钟**：内部 HSI 8 MHz，ADC 预分频 ÷6 → 1.33 MHz
- **ADC1**：12-bit 分辨率，单次转换，0–3300 mV 映射
- **TIM2**：0.5 ms 中断周期，生成 10 ms / 100 ms / 500 ms / 2000 ms 时间标志
- **USART1**：115200 bps，8N1，接收中断模式
- **IWDG**：LSI 40 kHz，预分频 ÷256，重载值 469，超时 ≈ 3.0 s
- **LCD1602**：4-bit 并行模式，显示 ADC 电压、报警状态、风险分数及配置菜单

## 固件架构

### 主循环调度

主循环采用时间片轮询架构，由 TIM2 中断标志驱动：

| 时间片 | 任务 |
|--------|------|
| 10 ms | 轮询地面站 USART 命令 |
| 100 ms | ADC 采样、LCD 刷新、LED 更新 |
| 500 ms | 融合算法执行、遥测帧上报、蜂鸣器控制 |
| 2000 ms | （预留心跳） |

### 报警状态机

```
            risk ≥ 1              risk ≥ 4
  NORMAL ──────────► WARN ──────────────► ALARM_L1
    ▲                  │                      │
    │   stable 2.5s    │   stable 2.5s        │  risk 持续 2s
    ◄──────────────────┘                      ▼
                                          ALARM_L2
                                              │
                                 risk 持续 2s  │
                                              ▼
                                          ALARM_L3
                                              │
                                 stable 2.5s   │
                                              ▼
                                          ALARM_L2  → ...逐级退出
```

**风险评分规则**：

- `adc_mv < WARN_THRESHOLD` → 0 分（NORMAL）
- `WARN ≤ adc_mv < ALARM` → 1–3 分（线性映射）
- `adc_mv ≥ ALARM` → 4–10 分（线性映射）

**默认阈值**：WARN = 1200 mV，ALARM = 2200 mV（可通过串口命令或按键修改）

### Flash 配置存储

采用 A/B 双页机制（地址 `0x0800F800` / `0x0800FC00`），每页包含 magic、version、CRC16 校验。写入时递增序列号并交替写入，确保断电安全。读取时比较序列号取最新有效页。

### 上电自检 (POST)

1. 读取并清除 IWDG 复位标志
2. LED 三灯轮检（PA1 → PA2 → PA3，各 100 ms）
3. 蜂鸣器双响（80 ms 开 / 80 ms 灭 × 2）
4. ADC 范围验证（5 mV < adc_mv < 3290 mV）
5. 上报 `@SELFTEST` 帧

## 通信协议

### 帧格式

所有帧以 `@` 开头，字段以逗号分隔，以 `*XX`（XOR 校验和，2 位十六进制）结尾，以 `\r\n` 结束。

```
@TYPE,field1=value1,field2=value2*XX\r\n
```

### 帧类型

| 帧类型 | 方向 | 说明 |
|--------|------|------|
| `@ENV` | MCU → PC | 环境遥测数据（seq, ms, light, state, level, risk, err） |
| `@EVT` | MCU → PC | 事件通知（状态变迁、配置变更、按键等） |
| `@CFG` | MCU → PC | 当前配置参数上报 |
| `@STAT` | MCU → PC | 运行统计信息 |
| `@LOG` | MCU → PC | 事件日志条目 |
| `@ERR` | MCU → PC | ADC 故障上报 |
| `@ACK` | MCU → PC | 命令执行确认 |
| `@NACK` | MCU → PC | 命令拒绝（含错误原因） |
| `@CMD` | PC → MCU | 地面站命令 |

### 支持的地面站命令

| 命令 | 说明 |
|------|------|
| `STAT?` | 查询运行统计 |
| `CFG?` | 查询当前配置 |
| `LOG?` | 查询事件日志 |
| `CLRLOG` | 清除事件日志 |
| `SET,<key>=<value>` | 设置参数（LHW/LHA/UP/BZ/DEMO/FLT） |
| `SAVE` | 将当前配置写入 Flash |
| `DEFAULT` / `RESET` | 恢复出厂默认配置 |
| `DEMO=ON` / `DEMO=OFF` | 切换 Demo 模式 |

## Web 地面站

### 快速启动

```bash
cd ground_station

# 安装依赖（仅 serial_probe.py 需要）
pip install -r requirements.txt

# 启动本地服务器
python visualizer.py

# 浏览器将自动打开 http://127.0.0.1:8000
```

### 功能面板

- **Live Telemetry** — 实时显示 ADC 电压、报警状态、风险分数
- **ADC Trend** — Canvas 绘制的 PA0 电压趋势曲线，含 WARN/ALARM 阈值参考线
- **Status** — 固件模式、报警源、MCU 运行时间、帧序号、错误码
- **Commands** — 快捷命令按钮 + 自定义命令输入框
- **Console** — 协议终端，显示所有收发帧（含时间戳和校验结果）
- **AI Assistant** — 可选的 DeepSeek AI 分析（需配置 `DEEPSEEK_API_KEY` 环境变量）
- **CSV Export** — 一键导出当前遥测历史为 CSV 文件

### 串口探针

```bash
# 被动监听串口数据（5 秒）
python serial_probe.py --port COM3 --no-cmd --seconds 5

# 发送命令并监听响应
python serial_probe.py --port COM3 --cmd "STAT?" --seconds 3

# 原始字节模式
python serial_probe.py --port COM3 --raw
```

### DeepSeek AI 集成

设置环境变量后，地面站可通过 `/api/deepseek` 端点将遥测数据发送至 DeepSeek API 进行智能分析：

```bash
export DEEPSEEK_API_KEY="sk-your-key-here"
python visualizer.py
```

未配置 API Key 时，分析助手将自动回退到本地规则引擎。

## Proteus 仿真

1. 使用 Proteus 8.x 打开 `proteus/EnvAlarm_STM32F103.pdsprj`
2. 在 Keil MDK 中编译固件生成 HEX 文件
3. 将 HEX 文件加载至 Proteus 中的 STM32F103C8 元件
4. 运行仿真，通过虚拟串口连接地面站

## 开发环境

- **IDE**：Keil MDK-ARM v5（uVision5）
- **MCU**：STM32F103C8T6（Cortex-M3, 64 KB Flash, 20 KB SRAM）
- **固件库**：STM32F10x Standard Peripheral Library（寄存器直接操作，无 HAL 依赖）
- **仿真**：Proteus 8.x Professional
- **地面站**：Python 3.8+、现代浏览器

## 编译与烧录

1. 使用 Keil 打开 `keil/STM32.uvprojx`
2. 选择 Target 1，点击 **Build** (F7) 编译
3. 通过 ST-Link 或 J-Link 烧录至 STM32F103C8
4. 连接 USART1（PA9/PA10）至 PC 的 USB-Serial 适配器
5. 启动地面站 `python ground_station/visualizer.py`

## 技术亮点

- **零依赖固件** — 全部采用寄存器直接操作，无 HAL/LL 库开销，代码体积极小
- **确定性调度** — TIM2 中断驱动的多级时间片，保证任务执行时序
- **断电安全配置** — Flash A/B 双页 + CRC16 + 序列号，防止配置丢失
- **健康喂狗策略** — IWDG 仅在 10ms/100ms/500ms 三个时间片全部正常执行后才喂狗，任一时间片卡死即在 3 秒内触发复位
- **完整协议栈** — XOR 校验、ACK/NACK 确认、事件日志、统计上报、ADC 故障检测
- **前后端协议一致** — 地面站前端 `app.js` 的命令解析与帧组装逻辑与固件 `protocol.c` 完全对齐
- **AI 辅助分析** — 地面站集成 DeepSeek API，可选的在线智能遥测分析

## 许可证

本项目仅供学习参考。
