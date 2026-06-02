# STM32F103 地面站

当前版本是”固定遥测源”展示版：页面加载后自动进入在线状态，并持续生成与当前 `final_project_main` 固件字段一致的协议帧。它不会读取或修改 Proteus 工程文件，也不会修改 Keil/固件文件。

## 适配目标

- MCU：STM32F103C8
- 串口参数展示：USART1，115200，8N1
- 协议格式：`@TYPE,key=value,...*CS\r\n`
- 校验：从 `@` 到 `*` 前一字节逐字节 XOR，两位十六进制大写
- 固件模式：`PRO_LITE`（`MODE=LITE` 命令兼容返回 `MODE=PRO_LITE`）
- 有效传感器：PA0 ADC，协议字段为 `light=<mV>`
- 不可用传感器：`temp/humi/gas` 固定显示为 `N/A`
- 本地命令模拟：按 `CODE/protocol.c` 分支返回 `STAT?`、`CFG?`、`LOG?`、`CLRLOG`、`SET`、`SAVE`、`RESET/DEFAULT`、`MODE=...` 的协议帧

## 文件

```text
ground_station/
├── index.html          # Web 地面站页面
├── app.js              # 固定遥测流、协议解析、命令响应、CSV 导出
├── styles.css          # 仪表盘样式
├── visualizer.py       # 本地 HTTP 服务 + 可选 DeepSeek 代理
├── serial_probe.py     # 命令行串口探针，保留备用
├── requirements.txt    # Python 依赖
└── README.md           # 本说明
```

## 启动

```powershell
cd "F:\University\Course\Sophomore xia\MCU\Proteus Project\final_project_main\ground_station"
pip install -r requirements.txt
python visualizer.py
```

默认地址：

```text
http://127.0.0.1:8000/index.html
```

如果 8000 端口被占用：

```powershell
python visualizer.py --port 8010
```

如果不想自动打开浏览器：

```powershell
python visualizer.py --no-browser
```

## 展示行为

- 页面加载后自动显示 `LINK ONLINE`。
- 默认每 500ms 生成一帧 `@ENV`，字段为当前固件兼容的 `PRO_LITE` 单 ADC 格式；`SET,UP=...` 会同步调整本地模拟上传间隔。
- 终端窗口会持续出现 `@ENV` 和关键 `@EVT`。
- `STAT?` 返回 `@STAT,ALARM_TOTAL=...,WARN_TOTAL=...,FAULT_TOTAL=...,MAX_LEVEL=...,L_MAX=...,L_MIN=...,RUN=...,KEY=...,SAVE=...,DROP=...,OVF=...`。
- `CFG?` 返回 `@CFG,LLW=0,LHW=...,LLA=0,LHA=...,THW=0,THA=0,HLW=0,HHW=0,HLA=0,HHA=0,GHW=0,GHA=0,UP=...,MODE=PRO_LITE,BZ=...,SEQ=...`。
- `LOG?` 返回本地事件环形日志的 `@LOG,idx=...,ts=...,evt=...,lv=...,st=...,risk=...,err=...,light=...`，最后返回 `@LOG,END,count=...`；`CLRLOG`/`LOGCLR` 清空本地事件日志并返回 `@ACK,cmd=CLRLOG`。
- `SET,LHW=...`、`SET,LHA=...`、`SET,UP=...`、`SET,BZ=...` 成功时返回 `@ACK,cmd=SET,key=...,val=...`；解析成功但值超出固件允许范围或键不支持时返回 `@NACK,cmd=SET,err=BAD_VALUE,key=...`。
- `SAVE` 本地模拟保存成功，返回 `@ACK,cmd=SAVE`。
- `RESET`/`DEFAULT` 恢复默认配置，返回 `@ACK,cmd=DEFAULT`，并追加 `@EVT,msg=CFG_DEFAULT_RESTORED,ms=0`。
- `MODE=PRO_LITE`/`MODE=LITE` 返回 `@ACK,cmd=MODE=PRO_LITE`，并追加 `@EVT,msg=MODE_PRO_LITE,ms=0`；`MODE=PRO`/`MODE=BASIC` 返回 `@NACK,cmd=MODE,err=HW_SINGLE_ADC_ONLY`。

## 不影响 Proteus 的边界

本展示版只运行和修改 `ground_station/`。不要为了地面站修改这些文件：

- `EnvAlarm_STM32F103.pdsprj`
- `*.workspace`
- `STM32.uvprojx`
- `STM32.uvoptx`
- `Objects/`
- `CODE/`
- `main.c`
