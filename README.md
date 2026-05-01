# STM32 环境监测与报警系统

这是一个基于 STM32F103C6T6 的环境监测课程项目。系统在 Proteus 中仿真运行，固件使用 Keil MDK 编译，网页地面站通过浏览器串口读取遥测数据。

项目覆盖光照、温湿度和气体模拟量采集，支持报警分级、事件日志、阈值配置、看门狗和掉电配置保存。

## 功能

- STM32F103C6T6 固件，基于标准外设库开发
- Proteus 仿真工程，包含 MCU、传感器输入、按键、蜂鸣器、数码管和串口终端
- 光照、温度、湿度、气体四通道采集
- Basic / Pro 两种运行模式
- 报警等级 L1-L3，支持按键手动升降级和 2 秒自动升级
- 串口协议带 XOR 校验，降低误帧对上位机显示的影响
- 网页地面站，支持实时曲线、事件日志、阈值写入、CSV 导出和演示模式
- DeepSeek AI 助手，支持主动分析遥测状态和自然语言问答

## 目录

```text
.
├── Keil工程/          Keil MDK 工程和 STM32 源码
│   ├── CODE/          外设驱动和应用模块
│   └── USER/          main.c、系统类型定义和入口代码
├── proteus工程/       Proteus 仿真工程
├── src/               网页地面站和串口辅助脚本
│   ├── index.html     地面站页面
│   ├── styles.css     页面样式
│   ├── app.js         Web Serial、协议解析和曲线绘制
│   ├── visualizer.py  本地网页服务器启动器
│   └── serial_probe.py 串口联调脚本
└── requirements.txt   Python 辅助脚本依赖
```

## 硬件与仿真配置

| 项目 | 配置 |
| --- | --- |
| MCU | STM32F103C6T6 |
| Keil Target Xtal | 8 MHz |
| Proteus MCU clock | 72 MHz |
| 串口 | USART1, 115200 8N1 |
| 地面站连接 | Chrome / Edge Web Serial API |

Proteus 中如果没有外部晶振电路，仍需要把 MCU 属性里的 `Clock Frequency` 设置为 `72MHz`，这样仿真时钟与固件的系统时钟配置一致。

## 快速开始

### 1. 编译固件

1. 用 Keil MDK 打开 `Keil工程/project.uvprojx`。
2. 确认 Target 里的 `Xtal` 为 `8.0 MHz`。
3. 编译工程，生成 HEX 文件。

### 2. 运行 Proteus 仿真

1. 打开 `proteus工程/final_project.pdsprj`。
2. 确认 MCU 的 `Clock Frequency` 为 `72MHz`。
3. 将 Keil 生成的 HEX 加载到 MCU。
4. 运行仿真，虚拟终端应能看到 `@EVT`、`@ENV` 等串口帧。

### 3. 启动网页地面站

安装 Python 依赖：

```powershell
pip install -r requirements.txt
```

启动本地页面：

```powershell
python src/visualizer.py
```

如果需要连接真实 DeepSeek，在启动前设置环境变量。密钥只保存在本机环境变量中，不会写入网页或仓库：

```powershell
$env:DEEPSEEK_API_KEY="sk-your-key"
python src/visualizer.py
```

网页 AI 面板默认调用本地代理 `/api/deepseek`。未配置密钥时，面板仍可使用本地规则诊断，适合课堂演示或无网络环境。

也可以手动启动静态服务器：

```powershell
cd src
python -m http.server 8000
```

然后用 Chrome 或 Edge 打开：

```text
http://127.0.0.1:8000/index.html
```

如果暂时没有串口或 Proteus COMPIM，点击页面上的“启动演示”可以查看界面和曲线效果。

## 串口协议

MCU 上传帧格式：

```text
@ENV,seq=1,ms=1000,mode=PRO,light=1800,temp=26.0,humi=55.0,gas=1200,state=NORMAL,level=0,risk=0,src=NONE,err=00*CS
```

命令帧格式：

```text
@CMD,STAT?*CS
@CMD,CFG?*CS
@CMD,SET,LHA=2300*CS
@CMD,RESET*CS
```

`CS` 是从 `@` 开始到 `*` 前一个字符的逐字节 XOR 校验值，使用两位十六进制表示。

## 常用命令

| 命令 | 作用 |
| --- | --- |
| `STAT?` | 查询运行统计 |
| `CFG?` | 查询当前阈值配置 |
| `LOG?` | 输出事件日志 |
| `CLRLOG` | 清空事件日志 |
| `SET,LHA=2300` | 写入光照高报警阈值 |
| `MODE=BASIC` | 切换到 Basic 模式 |
| `MODE=PRO` | 切换到 Pro 模式 |
| `SAVE` | 保存配置到 Flash |
| `RESET` | 恢复默认配置 |

## Python 串口探测

`src/serial_probe.py` 用于命令行联调串口：

```powershell
python src/serial_probe.py --port COM2 --baud 115200 --cmd "STAT?" --seconds 5
```

## 上传 GitHub 前建议

- 不上传 Keil `Objects/`、`Listings/`、Proteus workspace、缓存目录和本地备份。
- 如果仓库需要包含课程报告或演示视频，建议放到 Release，不建议直接提交大文件。
- 如果要公开仓库，先确认 Proteus 工程和参考资料没有版权限制。

## 许可证

本项目尚未指定许可证。公开到 GitHub 前建议补充 `LICENSE` 文件。
