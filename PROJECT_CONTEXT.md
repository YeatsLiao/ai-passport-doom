# AI-Passport Doom 移植项目 — 上下文文件

> **每次开新对话时，让 AI 先读这个文件即可恢复全部上下文。**

---

## 一句话概述

将真正的 Doom（GBADoom / PrBoom 引擎）移植到 AI-Passport 电子胸牌（ESP32-C3，无 PSRAM），用 3 个按键玩 E1M1。

---

## ⚠️ 重要：过程文章必须同步更新

`过程文章/` 目录（不进 git，本地保存）记录从 0 到 1 的全过程。
**每次有重要进展时，必须同步更新对应文章，不等用户提醒。**

| 什么时候记录 | 更新哪个文件 |
|-------------|-------------|
| 编译报错并修复 | `03-*.md` 的「当前状态」+ 新增修复记录 |
| 架构/方案变更 | 对应章节 |
| 发现新技术细节 | 相关章节 |
| 完成一个 Phase | 进度列表 |
| 烧录/调试新发现 | `04-*.md` |

记录格式：日期 + 问题现象 + 根因 + 修复方案 + 经验教训。

---

## 硬件规格

| 项目 | 规格 |
|------|------|
| MCU | ESP32-C3，RISC-V 单核 160 MHz |
| SRAM | ~400 KB（**无 PSRAM**） |
| Flash | 8 MB |
| 屏幕 | ST7789P3 240×320 RGB565，4-line SPI @ 40MHz |
| 按键 | UP / DOWN / OK（GPIO0 ADC 分压，3 档） |
| 音频 | ES8311 I2S codec（麦克风 + 喇叭） |
| 串口 | COM4 |
| 固件框架 | ESP-IDF 5.5.3 |
| 参考项目 | `D:\2.Project\ai-passport`（folotoy/ai-passport 原版） |

---

## 关键决策记录

### 1. 引擎选型：GBADoom（doomhack/GBADoom）

- 仓库：https://github.com/doomhack/GBADoom （上游原版，294⭐）
- Kippykip/GBADoom 是 fork，改进已合并回上游，**不用 fork**
- 基于 PrBoom 引擎，纯 C 代码
- 已在 256KB RAM 的 STM32（Prusa Core One, 2026.6）上验证可运行
- 预估帧率：5-15 FPS

### 2. 按键映射（长按区分模式）

| 模式 | UP 键 | DOWN 键 | OK 键 |
|------|-------|---------|-------|
| **默认（射击模式）** | 前进 | 后退 | 短按=开枪/使用 |
| **转向模式**（长按 OK 500ms 进入） | 左转 | 右转 | 短按=退出转向 |

- 屏幕角落显示 `↰↱` / `↑↓` 图标提示当前模式
- 开门/使用 = 引擎自动判断（走到门前按 OK）

### 3. WAD 文件（三级管线）

- 原始 `DOOM1.WAD`（shareware ~4.19MB）**不能直接烧**
- 必须经三级处理：
  1. `merge_pwad.py` 合并 `gbadoom.wad` 补丁 + 大写化 PNAMES → `DOOM1_GBA.WAD`
  2. `GbaWadUtil.exe` 转换 seg_t 格式（12→28 字节）→ `DOOM1_PROCESSED.WAD`
- **最终烧录 `DOOM1_PROCESSED.WAD`**（~3.90MB）
- 存入 Flash 专用分区（offset 0x310000）

---

## 项目目录结构

```
D:\2.Project\ai-passport\          ← 原版 AI-Passport 固件（参考 BSP）
D:\2.Project\GBADoom\              ← 已 clone 的 GBADoom 源码
D:\2.Project\ai-passport-doom\     ← 本项目
├── CMakeLists.txt                 ← 顶层 CMake（GBADOOM_PATH 指向 ../GBADoom）
├── partitions.csv                 ← factory 3MB + wad 4.06MB
├── sdkconfig.defaults             ← 关闭 BLE/WiFi，优化内存
├── DOOM1.WAD                      ← Shareware WAD 文件（~4.19MB）
├── main/
│   ├── CMakeLists.txt
│   └── main.c                     ← app_main + FreeRTOS Doom task
├── components/
│   ├── bsp_doom/                  ← 精简 BSP（显示 + 按键）
│   │   ├── include/bsp_pins.h     ← 硬件引脚定义
│   │   ├── include/bsp_doom.h     ← BSP 接口
│   │   └── src/bsp_display.c     ← ST7789P3 SPI 显示
│   │   └── src/bsp_button.c      ← ADC 三键输入
│   └── doom/                      ← GBADoom 引擎封装
│       ├── CMakeLists.txt         ← GLOB GBADoom 源码 + 排除列表
│       ├── i_system_esp32.c       ← ESP32 平台层（12 个 e32 函数）
│       ├── esp32_wad.c            ← WAD Flash mmap 加载
│       └── doom_iwad.h            ← 覆盖原版头文件（指针代替数组）
```

---

## GBADoom 平台接口分析（需要替换的文件）

### 必须重写的平台层文件

| 文件 | 职责 | 替换为 |
|------|------|--------|
| `source/i_system_gba.cpp` | GBA 硬件初始化、屏幕双缓冲、输入轮询、错误处理 | `i_system_esp32.c` |
| `source/i_video.c` | 调用 `I_GetBackBuffer` 等视频接口 | 适配 ESP32 SPI 显示输出 |
| `source/i_audio.c` | GBA maxmod 音频 | ES8311 I2S 音频输出 |
| `source/i_main.c` | 程序入口 | ESP-IDF `app_main()` |
| `source/fixeddiv.s` | ARM 汇编定点除法 | RISC-V C 实现或 `__divsi3` |
| `include/gba_functions.h` | GBA DMA、BIOS 调用、SRAM 存取 | `memcpy`/`memset`/`esp_flash_read` |
| `include/config.h` | 编译配置 | 添加 `#define ESP32` 分支 |

### 关键平台接口函数（`i_system_e32.h` 定义）

```c
void I_InitScreen_e32();                    // 初始化屏幕 + 定时器
void I_CreateBackBuffer_e32();              // 创建帧缓冲
int  I_GetVideoWidth_e32();                 // 返回 120（GBA 半分辨率）
int  I_GetVideoHeight_e32();                // 返回 160
void I_FinishUpdate_e32(buf, pal, w, h);    // 将帧缓冲刷到屏幕
void I_SetPallete_e32(const byte* pal);     // 设置 256 色调色板
void I_ProcessKeyEvents();                  // 轮询按键并投递事件
int  I_GetTime_e32(void);                   // 获取 tick（35fps 计时）
unsigned short* I_GetBackBuffer();          // 获取后缓冲指针
unsigned short* I_GetFrontBuffer();         // 获取前缓冲指针
void I_Error(const char* error, ...);       // 致命错误显示
void I_Quit_e32();                          // 退出清理
```

### GBADoom 渲染参数

- 内部分辨率：**120×80**（GBA MODE4 半分辨率，原始 Doom 320×200 缩到 1/4）
- 帧缓冲格式：8-bit 索引色（256 色调色板）
- 显示输出：GBA MODE4（页翻转双缓冲，240×160 RGB555）

---

## 实施计划（6 阶段）

### Phase 0：环境准备 ✅
- [x] clone ai-passport 原版项目
- [x] clone GBADoom 源码 (doomhack/GBADoom)
- [x] 硬件连接（COM4）
- [x] 选型分析（GBADoom 优于 Doomgeneric/MG21DOOM 等）
- [x] 获取 DOOM1.WAD（shareware ~4.19MB）

### Phase 1：ESP-IDF 工程骨架 ✅
- [x] 创建 ESP-IDF 项目结构（CMakeLists.txt, main/, components/）
- [x] bsp_doom 组件（从 ai-passport 精简的显示+按键 BSP）
- [x] doom 组件（GBADoom 源码 + ESP32 平台层 + WAD Flash 加载）
- [x] partitions.csv（factory 3MB + wad ~4.94MB）
- [x] sdkconfig.defaults（关闭 BLE/WiFi 节省 RAM）
- [x] `idf.py build` 编译验证（7 轮修复，详见第 3 篇）

### Phase 2：平台层适配 ✅
- [x] `i_system_esp32.c`：12 个 e32 接口全部实现
- [x] `gba_functions.h`：非 GBA 分支已用 memcpy/memset，无需改
- [x] `fixeddiv.s`：非 GBA 的 FixedDiv 用 int_64_t C 实现，无需汇编
- [x] 编译调错（7 轮修复，详见第 3 篇）
- [x] 目标：编译通过，能进入 Doom 主循环

### Phase 3：WAD 加载与显示 ✅
- [x] WAD Flash 分区 + mmap 加载（esp32_wad.c 已写）
- [x] 调色板 RGB→RGB565 转换 + byte swap（i_system_esp32.c 已实现）
- [x] WAD 三级管线（DOOM1.WAD → merge → GbaWadUtil → DOOM1_PROCESSED.WAD）
- [x] 2x 纵向缩放全屏显示 + 底部黑色遮挡条

### Phase 4：游戏可玩性（进行中）
- [x] 基础三键输入（UP/DOWN/OK 映射）
- [x] Z_Malloc 崩溃修复（sdkconfig 精简 + LWIP 裁剪）
- [x] 菜单无法进入修复（引擎层解耦 key_use）
- [x] 底部重复修复（黑色遮挡条，代码精简至最小改动）
- [ ] 性能调优（跳帧、降低内部分辨率、裁剪特效）
- [ ] 目标：E1M1 可玩，帧率 ≥5 FPS

### Phase 5：收尾
- [ ] 音效适配（可选，ES8311 I2S）
- [ ] 存档支持（NVS 存储）
- [ ] UI 美化（模式图标、电量显示）
- [ ] 文档撰写、录制演示视频

---

## 内存预算（修正版）

| 用途 | 大小 | 说明 |
|------|------|------|
| FreeRTOS + 驱动 | ~80 KB | 系统最小占用 |
| 帧缓冲 | ~38 KB | 120×160×16bit 单缓冲 |
| Zone 内存池 | ≤256 KB | malloc+缩小循环，实际可能 150-220KB |
| WAD 数据 | 0 KB | mmap 从 Flash 读取，不占 RAM |
| **合计** | **~374 KB** | 在 ~400KB 可用预算内（紧张） |

---

## 风险清单

| 风险 | 概率 | 缓解 |
|------|------|------|
| RAM 不够（编译或运行 OOM） | 高 | 降低 zone maxHeapSize 到 180KB；参考 MG21DOOM 优化 |
| 帧率太低（<3 FPS） | 中 | 降低内部分辨率到 80×50；裁剪怪物数量 |
| Flash 不够存 WAD | 低 | DOOM1.WAD ~4MB，8MB Flash 足够 |
| 编译链接错误 | 中 | 预计有头文件/符号/类型问题，需逐个修复 |
| ESP-IDF 5.5.3 兼容性 | 低 | GBADoom 是纯 C，兼容性高 |
