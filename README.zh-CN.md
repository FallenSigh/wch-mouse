# wch-mouse

[English](README.md) | [简体中文](README.zh-CN.md)

基于 **WCH CH585**（RISC-V RV32IMC + Zba/Zbb/Zbc/Zbs/Xw）的无线游戏鼠标与
2.4G 接收器 dongle 的裸机固件。

一个 CMake 工程构建两个镜像：

| 目标 | 固件 | 源码 |
| --- | --- | --- |
| `wch-mouse` | 鼠标本体 | `src/` |
| `wch-dongle` | 2.4G 接收器（RF 主机 → USB HID） | `dongle/` |

两者共享 `cmake/wch_sdk.cmake` 中的厂商 SDK / HAL / USB 设备层 / RF 链路配置，
因此一处驱动或描述符改动会同时生效于两个固件。

## 特性

- **三条链路，共用一块射频。** USB 始终可用，只要主机连接就优先走 USB；否则鼠标
  运行 2.4G RF 链路（低延迟，需要 dongle）或蓝牙 LE HID 外设——因为只有一块硬件
  射频，模式在**开机时**选定，不支持运行时切换。
- **PAW3395** 光学传感器，运动数据通过 SPI0 中断驱动读取。
- **BMI270** IMU 驱动空中鼠标（air-mouse）模式：按住侧键1 + 侧键2，把光标从光学
  传感器交给陀螺仪，再按一次切回。
- **SSD1315** OLED 状态面板、**WS2812** 底部灯效、**ERM** 振动马达。
- **电池**：BQ24075 充电器 + VBAT 监测，带深度待机路径。
- **主机配置**通过厂商自定义的 HID Feature 报告进行——可走 USB，也可经 dongle 无线进行。

## 硬件

| 模块 | 器件 / 接口 |
| --- | --- |
| MCU | WCH CH585，448 KiB Flash @ `0x0`，128 KiB RAM @ `0x20000000` |
| 光学传感器 | PAW3395，SPI0（运动信号走 PA7 下降沿，SPI0 DMA 读取） |
| IMU | BMI270，SPI1（仅用于 air-mouse 的陀螺仪） |
| 显示屏 | SSD1315 OLED，I2C（`PB20`/`PB21`） |
| 灯效 | WS2812（TIM1 + DMA） |
| 马达 | ERM 振动马达 |
| 充电 | BQ24075（`PGOOD` `PB6` / `CHG` `PB5`）+ `BT_VOL` ADC（`PA3`） |
| 健康模块 | UART3 上的外部 BIO 模块（`JFC103` 帧格式） |

完整引脚映射见 [`docs/io.csv`](docs/io.csv)，并镜像到
[`src/bsp/board.h`](src/bsp/board.h)——后者是修改板级接线的唯一入口。

## 仓库布局

```
src/               鼠标应用代码
  app/             鼠标、air-mouse、电台模式、电池、马达、BIO、设置、协议
  bsp/             板级引脚映射 (board.h)、非易失存储 (nvm)、USB HID 设备、UART、日志
  display/         I2C 总线、SSD1315 面板胶水层 + vendored LibDriver 内核
  led/             WS2812 驱动与 RGB 动画
  sensor/         PAW3395 驱动 + BMI270/BMI2（vendored Bosch API）及其 port
  transport/       传输路由 + USB / RF / BLE 后端，共享 rf_cfg.h 空中协议契约
dongle/            2.4G 接收器固件（RF 主机 -> USB HID）
cmake/             共享构建：源文件列表、编译选项、链接选项、工具链
ble/               厂商 fork 的 BLE HAL + GATT profile + 预编译闭源库
StdPeriphDriver/   厂商外设驱动（请勿修改）
Startup/ RVMSIS/   厂商启动汇编与类 CMSIS 头（请勿修改）
Ld/Link.ld         自定义链接脚本
docs/              datasheet + io.csv 引脚映射（docs/CH585EVT 下 75 MB 的厂商 SDK 仅本地保留）
scripts/           主机工具 (mouse_proto.py)、MounRiver 刷写助手、udev 规则
```

## 快速开始

### 前置条件

- **MounRiver / WCH RISC-V GCC 工具链**（`riscv32-wch-elf-gcc`）。直接
  `cmake -B build` 会按设计失败：没有工具链文件时构建会 `FATAL_ERROR`。工具链查找
  顺序为 `$WCH_TOOLCHAIN_ROOT`、`/opt/wch/riscv-wch-gcc15`、`~/MounRiver/...`；可用
  `-DWCH_TOOLCHAIN_ROOT=/path` 覆盖。
- CMake ≥ 3.21，以及 Ninja（或 Make）。
- `wchisp` 在 `PATH` 中，用于简单的刷写目标。
- Python 3 + `hidapi`（`pip install hidapi`），用于主机配置工具。

### 构建

```sh
cmake --preset wch          # 指定工具链并配置 build/
cmake --build build         # 两个固件 -> build/wch-mouse.* 与 build/dongle/wch-dongle.*

cmake --build build --target wch-mouse     # 或只构建其中一个
cmake --build build --target wch-dongle
```

### 配置项

- `WCH_BLE_ENABLE` — 链接 BLE（外设）传输。
- `WCH_RF_ENABLE` — 引入 2.4G RF 传输。BLE 与 RF 共用同一块射频，两者同时开启会
  构建出"开机二选一"的双射频镜像。
- `WCH_DCDC_ENABLE` — 启用内部 DC-DC；需要 `VSW` 与 `VDCID` 之间的 10 µH 电感，
  板上把两者短接时必须保持 `OFF`。
- `WCH_DEBUG_UART` — 把 `printf` 重定向到调试 UART（如 `-DWCH_DEBUG_UART=1`）。
- `WCH_LOG_LEVEL` — 日志级别（`-DWCH_LOG_LEVEL=LOG_LVL_DEBUG`）。

`compile_commands.json` 是指向 `build/` 的符号链接，因此必须先完成配置步骤，
clangd/LSP 才能在 `src/` 上正常工作。

### 刷写

刷写使用外部工具，**不属于**默认构建的一部分。鼠标占据无后缀的目标名，dongle 的带
`-dongle` 后缀：

```sh
cmake --build build --target flash-usb          # wchisp -u flash
cmake --build build --target flash-serial       # wchisp -s flash
cmake --build build --target flash-mrs          # MounRiver CommunicationLib (libmcuupdate.so)

cmake --build build --target flash-usb-dongle   # dongle 的同类目标
```

`flash-mrs` 驱动的是 MounRiver Studio 自己使用的原生库，因此即使两线调试接口已关闭
也能刷写。

鼠标可以**无需断电**进入 ROM ISP 引导：在 USB 已连接的情况下按住 BOOT 拉带
（**PB22**）3 秒。该操作会先擦除 block 0，因此应用镜像不会存活、之后必须下载——
但 DataFlash 中的设置记录会保留。此路径受 VBUS 门控，所以用电池时按住不会擦掉镜像。
dongle 没有按键，沿用断电流程。

## 主机工具

鼠标暴露一个厂商自定义的 HID **Feature 报告**（usage page `0xFF00`，无 Report ID，
63 字节数据）。由于 HID 的 `GET_FEATURE` 不向主机返回负载，每次交互都是"先
`SET_FEATURE` 写请求，再 `GET_FEATURE` 读设备准备好的应答"两步。

```sh
pip install hidapi

# 让已登录用户访问 hidraw 节点（覆盖两个 product id）
sudo cp scripts/99-wch-mouse.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=hidraw

# 有线（product id 0xFE0C）
scripts/mouse_proto.py info
scripts/mouse_proto.py dpi 1600
scripts/mouse_proto.py rate 1000

# 无线，经 dongle（product id 0xFE0D）
scripts/mouse_proto.py --dongle info
```

完整列表见 `scripts/mouse_proto.py --help`：`version`、`info`、`radio`、
`battery`、`dpi`、`sensor`、`lift`、`air-sens`、`air-odr`、`rgb`、`oled`、
`rate`、`bio-acq`、`bio-sleep`、`periph`、`motor`、`motor-en`、`raw`。
鼠标运行 RF 时这些读写命令同样可用：dongle 转发请求，并把应答搭载在鼠标自己的帧里
带回。

## 架构

### 传输路由

`src/transport.c` 维护一个 `transport_t` 后端数组，第一个 `link_up()` 为真的后端胜出，
优先级为 **USB > RF > BLE**。后端位于 `src/transport/`：`transport_usb.c`、
`transport_rf.c`（RF 构建）、`transport_ble.c`（BLE 构建）。两个射频文件始终参与编译，
在对应宏缺失时编译为空实现。

### 2.4G RF 链路

`src/transport/rf_cfg.h` 是两个固件共享的空中协议契约：鼠标为设备/TX，dongle 为主机/RX。
dongle 会对收到的每个包回一个短 ACK，携带自己的接收计数，使鼠标能感知丢包；同一条反向
通道承载主机发往鼠标的 SET 命令，以及鼠标回给 dongle 的应答，由 dongle 提供给
`GET_FEATURE`。

### Radio模式

模式存放在 DataFlash 中，在**开机时**应用（厂商协议栈没有经过验证的运行时切换方案）。
按住**侧键1 + 滚轮点击 3 秒后松开**，即存储另一模式并复位进入。单射频构建在编译期固定
模式并忽略存储字节，因此一条陈旧记录无法关闭该构建唯一的后端。USB 不受模式影响。

### 配置与持久化

用户设置（DPI、传感器模式、抬升高度、RGB、OLED、回报率、air-mouse 预设、马达开关、
外设可用电池策略）通过 `src/bsp/nvm.c` 中的通用记录存储保存在 DataFlash 里，后者独占
ROM `EEPROM_*` 的 offset / 对齐 / 整块擦除等陷阱。`src/app/settings.c` 拥有该记录并把
它应用到硬件；其他模块通过访问器读取。

### 电源管理

空闲 30 秒后——在电池供电、非 air-mouse 模式、且运行 RF 时——外设被停用，核心进入待机（`src/app/power.c`）。唤醒源为运动引脚、按键/编码器和充电器。面板、LED
供电轨与 BIO 模块是最大的常态负载，默认仅在插线时运行，除非 `periph_batt` 覆盖项解除
该门控。

## 文档

- [`AGENTS.md`](AGENTS.md)——详尽的、带有明确倾向的项目笔记：构建细节、架构边界，
  以及一长串硬件相关的坑。
- [`docs/io.csv`](docs/io.csv)——引脚映射。
- `docs/*.pdf`——CH585、PAW3395、BMI270、BQ24075 与 TPS61222 的 datasheet。

## 第三方

厂商与 vendored 代码保留各自的许可与风格：WCH SDK（`StdPeriphDriver/`、`Startup/`、
`RVMSIS/`、`ble/` 及预编译库）、Bosch BMI270/BMI2 API（`src/sensor/`）、以及 LibDriver
SSD1315 内核（`src/display/ssd1315/`，MIT）。本仓库只维护这些目录之外的代码；应用、
传输与驱动 port 源码统一用 `clang-format` 格式化（K&R 大括号、4 空格缩进、100 列）。
