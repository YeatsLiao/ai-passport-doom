# AI-Passport Doom

将真正的 Doom（GBADoom / PrBoom 引擎）移植到 AI-Passport 电子胸牌。

- **硬件**：ESP32-C3（RISC-V 160MHz，无 PSRAM），ST7789P3 240×320 SPI 屏，3 按键
- **引擎**：[GBADoom](https://github.com/doomhack/GBADoom)（PrBoom 分支，纯 C）
- **关卡**：DOOM1 Shareware E1M1
- **帧率**：5-15 FPS（预估）

## 项目结构

```
├── main/                       ← app_main + FreeRTOS Doom task
├── components/
│   ├── bsp_doom/               ← 显示 (ST7789P3 SPI) + 按键 (ADC)
│   └── doom/                   ← GBADoom 引擎封装
│       ├── i_system_esp32.c    ← ESP32 平台层（显示/输入/计时）
│       └── esp32_wad.c         ← WAD Flash mmap 加载
├── tools/
│   ├── merge_pwad.py           ← IWAD + PWAD 合并 + 大写化修复
│   ├── merge_wad.py            ← 固件 + WAD 合并为完整镜像
│   └── build.sh                ← 编译 + 合并脚本
├── partitions.csv              ← factory 3MB + wad ~4.94MB
└── sdkconfig.defaults          ← 关闭 BLE/WiFi，优化内存
```

## 环境要求

- **ESP-IDF** v5.5.x（推荐 5.5.3+）
- **Python** 3.x（脚本用）
- **GbaWadUtil.exe**（GBADoom 仓库自带，用于 WAD 格式转换）
- **DOOM1.WAD**（Shareware 版，~4.19MB）

## 快速开始

### 0. 克隆仓库

本项目依赖 GBADoom 引擎源码，需同时克隆：

```powershell
# 克隆本项目
git clone https://github.com/YeatsLiao/ai-passport-doom.git
cd ai-passport-doom

# 克隆 GBADoom 引擎（必须放在同级目录）
git clone https://github.com/YeatsLiao/GBADoom.git
```

目录结构必须为：
```
├── ai-passport-doom/    ← 本仓库（ESP-IDF 工程）
├── GBADoom/             ← DOOM 引擎源码
```

> CMakeLists.txt 中通过 `GBADOOM_PATH` 指向 `../GBADoom`，如路径不同需修改。

### 1. 准备 WAD（首次）

**不能直接烧原始 DOOM1.WAD！** 必须经三级处理：

```powershell
# 合并 gbadoom.wad 补丁 + 大写化 PNAMES 修复
python tools/merge_pwad.py DOOM1.WAD ../GBADoom/GbaWadUtil/gbadoom.wad DOOM1_GBA.WAD

# GbaWadUtil 格式转换（seg_t 12→28 字节，GBADoom 引擎必须）
../GBADoom/GbaWadUtil/GbaWadUtil.exe -in DOOM1_GBA.WAD -out DOOM1_PROCESSED.WAD
```

| 跳步后果 | 错误信息 |
|---------|---------|
| 不合并 gbadoom.wad | `W_GetNumForName: STGANUM0 not found` |
| 不经过 GbaWadUtil | `P_GroupLines: Subsector a part of no sector!` |

### 2. 构建固件

```powershell
idf.py fullclean
idf.py build
idf.py merge-bin -o build/ai-passport-doom-firmware.bin
```

### 3. 烧录

**分步烧录**（推荐）：

```powershell
# 烧固件
idf.py -p COM4 flash

# 烧 WAD（首次需要）
esptool.py -p COM4 -b 460800 write_flash 0x310000 DOOM1_PROCESSED.WAD
```

**合并一次性烧录**：

```powershell
python tools/merge_wad.py build/ai-passport-doom-firmware.bin DOOM1_PROCESSED.WAD build/ai-passport-doom-full.bin 0x310000
esptool.py -p COM4 -b 460800 write_flash 0x0 build/ai-passport-doom-full.bin
```

### 4. 验证

```powershell
idf.py -p COM4 monitor
```

正常启动：标题画面显示，底部 16 行黑色遮挡条，按 OK 开始游戏。

## 按键映射

| 物理按键 | Doom 事件 | 游戏含义 |
|---------|----------|--------|
| UP | KEYD_UP | 前进 |
| DOWN | KEYD_RIGHT + KEYD_A | 右转 + 确认/开门 |
| OK | KEYD_B | 开枪 |

## 显示方案

- 引擎渲染 240×160（8bpp 索引色），纵向 2 倍拉伸到 240×320
- 底部 16 行黑色遮挡条，遮住面板重复行
- `0xB6 {0x00, 0x82}` 修复左侧行扫描偏移（防止内容往下掉）
- **已知限制**：左侧有轻微透明格子鬼影（ST7789P3 面板列驱动硬件特性，无法通过代码消除）
- 详细排查过程见 [第 5 篇](过程文章/05-display-bottom-repeat-fix.md) 和 [第 6 篇 §6](过程文章/06-flash-procedure.md#6-已知显示问题)

## 问题记录

### 已解决

#### 1. Z_Malloc 内存耗尽（红屏卡死）

**现象**：进入关卡或 demo 演示时 `Z_Malloc: failed on allocation of 36348 bytes`，红屏死机。

**根因**：
- ESP32-C3 无 PSRAM，可用 SRAM ~400KB，系统堆（FreeRTOS + 驱动 + WiFi）占 ~100KB+
- 原始 zone 256KB 太大，系统堆不够分配帧缓冲
- 缩小 zone 后大分配（36KB 纹理）溢出到系统堆也失败

**解决过程**：
1. zone 256KB → 80KB，添加 128KB 静态 overflow 缓冲区（.bss 段）
2. overflow 从 bump-only 升级为**空闲链表回收** + **分配列表追踪**
3. 发现 `Z_FreeTags` 只遍历 zone 链表，overflow 中的 PU_LEVEL 块在关卡切换时永远不被释放
4. 修复 `Z_FreeTags`：先遍历 overflow 分配列表，释放匹配 tag 的块
5. 发现 overflow 块 tag 被覆盖为 `PU_EXTERN(15)`，`Z_FreeTags` 按 tag 范围匹配不到
6. 改用**地址范围检查**识别 overflow 块，保留原始 tag（PU_LEVEL 等）
7. 发现 demo 循环结束时不触发 gamestate 变化，`Z_FreeTags` 不被调用
8. 在 `G_CheckDemoStatus` 中主动调用 `Z_FreeTags(PU_LEVEL, ...)`

**关键修改文件**：`z_zone.c`、`g_game.c`

#### 2. globals_t 过大导致 zone 碎片化

**现象**：zone 空间被 globals_t 中的大数组（drawsegs、openings、vissprites）占满。

**解决**：将 drawsegs/openings/vissprites 从 `globals_t` 移到 `r_hotpath.iwram.c` 中的静态数组，节省 ~20KB zone 空间。

#### 3. 帧缓冲分配失败

**现象**：`Framebuffer alloc failed! backbuffer=0x0`

**根因**：overflow 静态缓冲区太大（.bss 段），挤掉了系统堆空间。

**解决**：
- 帧缓冲（38KB）和行缓冲（8KB）从 `malloc` 改为 `.bss` 静态数组
- overflow 缓冲区保持 128KB
- DOOM 任务栈从 32KB 缩减到 16KB（大数组已移出）

#### 4. WiFi 配置无法持久关闭

**现象**：ESP-IDF 构建系统每次 build 会把 `CONFIG_ESP_WIFI_ENABLED` 重新设回 `y`。

**解决**：接受现状——WiFi 编译进固件但不调用 `esp_wifi_init()`，运行时不占额外内存。通过 `sdkconfig.defaults` + `SDKCONFIG_DEFAULTS` 变量确保配置被加载。

### 待解决

| 问题 | 状态 | 说明 |
|------|------|------|
| 左侧鬼影 | 面板硬件限制 | ST7789P3 列驱动串扰，轻微不影响游戏 |
| 帧率优化 | 未开始 | 预估 5-15 FPS，可尝试关闭音频/减少渲染行数 |
| 声音系统 | 已禁用 | ESP32-C3 RAM 不够同时跑音频，可考虑 I2S 输出 |

## 文档

| 文件 | 内容 |
|------|------|
| [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) | 项目上下文（新对话入口） |
| [过程文章/01](过程文章/01-idea-and-feasibility.md) | 可行性分析 |
| [过程文章/02](过程文章/02-deep-dive-gbadoom-source.md) | GBADoom 源码分析 |
| [过程文章/03](过程文章/03-esp-idf-project-skeleton.md) | ESP-IDF 工程搭建 |
| [过程文章/04](过程文章/04-flashing-guide.md) | 烧录指南（WAD 管线详解） |
| [过程文章/05](过程文章/05-display-bottom-repeat-fix.md) | 底部重复排查 |
| [过程文章/06](过程文章/06-flash-procedure.md) | 完整构建烧录流程 |
| [过程文章/07](过程文章/07-memory-optimization.md) | 内存优化全过程 |

## License

本项目代码遵循原 GBADoom 的 GPL 许可证。DOOM WAD 文件受 id Software 版权约束。
