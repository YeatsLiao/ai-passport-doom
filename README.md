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

- **ESP-IDF** v5.5.x（推荐 5.5.3）
- **Python** 3.x（脚本用）
- **GbaWadUtil.exe**（GBADoom 仓库自带，用于 WAD 格式转换）
- **DOOM1.WAD**（Shareware 版，~4.19MB）

## 快速开始

### 1. 准备 WAD（首次）

**不能直接烧原始 DOOM1.WAD！** 必须经三级处理：

```powershell
# 合并 gbadoom.wad 补丁 + 大写化 PNAMES 修复
python tools/merge_pwad.py DOOM1.WAD <GBADoom路径>/GbaWadUtil/gbadoom.wad DOOM1_GBA.WAD

# GbaWadUtil 格式转换（seg_t 12→28 字节，GBADoom 引擎必须）
<GbaWadUtil路径>/GbaWadUtil.exe -in DOOM1_GBA.WAD -out DOOM1_PROCESSED.WAD
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
- 详细排查过程见 [第 5 篇](过程文章/05-display-bottom-repeat-fix.md)

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

## License

本项目代码遵循原 GBADoom 的 GPL 许可证。DOOM WAD 文件受 id Software 版权约束。
