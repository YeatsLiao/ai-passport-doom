# AI-Passport Doom

在 AI-Passport 电子胸牌上运行 DOOM。

![硬件](https://img.shields.io/badge/MCU-ESP32--C3-blue) ![屏幕](https://img.shields.io/badge/Screen-ST7789P3%20240x320-green) ![帧率](https://img.shields.io/badge/FPS-5~15-yellow)

## 硬件

| 项目 | 规格 |
|------|------|
| MCU | ESP32-C3，RISC-V 160MHz，~400KB SRAM，**无 PSRAM** |
| Flash | 8MB |
| 屏幕 | ST7789P3 240×320 RGB565，SPI @ 40MHz |
| 按键 | UP / DOWN / OK（ADC 分压，3 档） |
| 引擎 | [GBADoom](https://github.com/doomhack/GBADoom)（PrBoom 分支，纯 C） |

## 项目结构

```
├── main/                       ← app_main + FreeRTOS Doom task
├── components/
│   ├── bsp_doom/               ← 显示 (ST7789P3 SPI) + 按键 (ADC)
│   └── doom/                   ← GBADoom 引擎封装
│       ├── i_system_esp32.c    ← ESP32 平台层（显示/输入/计时）
│       └── esp32_wad.c         ← WAD Flash mmap 加载
├── docs/                       ← 项目文档
│   ├── WAD-FILES.md            ← WAD 文件说明与管线
│   ├── issues.md               ← 问题记录（已解决 + 待解决）
│   ├── 01-project-context.md   ← 项目上下文
│   ├── 02-engine-selection.md  ← 引擎选型分析
│   └── 03-source-port-guide.md ← 源码移植参考
├── tools/                      ← WAD 处理与构建脚本
├── DOOM1.WAD                   ← 原版 Shareware
├── DOOM1_GBA.WAD               ← 合并 GBA 补丁后
├── DOOM1_PROCESSED.WAD         ← 最终烧录版（← 烧这个）
├── partitions.csv              ← factory 3MB + wad ~4.94MB
└── sdkconfig.defaults          ← 关闭 BLE/WiFi，优化内存
```

## 快速开始

### 1. 克隆

```powershell
# 本项目
git clone https://github.com/YeatsLiao/ai-passport-doom.git
cd ai-passport-doom

# GBADoom 引擎（必须放在同级目录）
git clone https://github.com/YeatsLiao/GBADoom.git
```

> CMakeLists.txt 中 `GBADOOM_PATH` 指向 `../GBADoom`，路径不同需修改。

### 2. 准备 WAD

**不能直接烧 DOOM1.WAD**，需经两步处理（详见 [WAD 文件说明](docs/WAD-FILES.md)）：

```powershell
# 第一步：合并 GBA 补丁
python tools/merge_pwad.py DOOM1.WAD ../GBADoom/GbaWadUtil/gbadoom.wad DOOM1_GBA.WAD

# 第二步：格式转换（seg_t 12→28 字节）
../GBADoom/GbaWadUtil/GbaWadUtil.exe -in DOOM1_GBA.WAD -out DOOM1_PROCESSED.WAD
```

### 3. 构建

```powershell
idf.py build
```

### 4. 烧录

```powershell
# 烧固件
idf.py -p COM4 flash

# 烧 WAD（首次需要，烧一次即可）
esptool.py -p COM4 -b 460800 write_flash 0x310000 DOOM1_PROCESSED.WAD
```

### 5. 验证

```powershell
idf.py -p COM4 monitor
```

## 按键映射

| 按键 | 游戏操作 |
|------|---------|
| UP | 前进 |
| DOWN | 右转 + 确认/开门 |
| OK | 开枪 |

## 显示

- 引擎渲染 240×160（8bpp），纵向 2x 拉伸到 240×320
- 底部 16 行黑色遮挡条（遮住面板重复行）
- 已知限制：左侧轻微鬼影（ST7789P3 面板硬件特性）

## 文档

| 文档 | 说明 |
|------|------|
| [WAD 文件说明](docs/WAD-FILES.md) | 各 WAD 用途、管线流程、烧哪个 |
| [问题记录](docs/issues.md) | 已解决 + 待解决问题 |
| [项目上下文](docs/01-project-context.md) | 硬件规格、决策记录、实施计划 |
| [引擎选型](docs/02-engine-selection.md) | 候选方案对比 |

## License

代码遵循 GBADoom 的 GPL 许可证。DOOM WAD 受 id Software 版权约束。
