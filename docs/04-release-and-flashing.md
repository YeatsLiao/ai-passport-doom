# 发布与烧录指南

## 构建固件

### 环境准备

需要 ESP-IDF v5.5.3+ 环境。Windows 下打开 **ESP-IDF CMD** 终端（开始菜单搜索）。

### 构建步骤

```powershell
# 进入项目目录
cd d:\Project\ai-passport-doom\ai-passport-doom

# 编译
idf.py build

# 合并固件（bootloader + 分区表 + 应用）
idf.py merge-bin -o build/ai-passport-doom-firmware.bin
```

### 生成一键烧录镜像

固件 + WAD 合并为完整 Flash 镜像：

```powershell
# 使用合并脚本
python tools/merge_all.py
```

产出两个文件：

| 文件 | 大小 | 说明 |
|------|------|------|
| `build/ai-passport-doom-firmware.bin` | ~786 KB | 仅固件（不含关卡数据） |
| `build/ai-passport-doom-full.bin` | ~6.8 MB | **完整镜像（固件 + 关卡，一键烧录）** |

---

## 手动烧录

### 方式一：一键烧录（推荐）

将整个 Flash 镜像一次性写入，适合首次烧录或完整恢复：

```powershell
esptool.py -p COM4 -b 460800 write_flash 0x0 build/ai-passport-doom-full.bin
```

### 方式二：分步烧录

分别烧固件和 WAD，适合只更新固件不重刷 WAD：

```powershell
# 第一步：烧固件（bootloader + 分区表 + 应用）
esptool.py -p COM4 -b 460800 write_flash 0x0 build/ai-passport-doom-firmware.bin

# 第二步：烧 WAD（关卡数据，首次烧一次即可）
esptool.py -p COM4 -b 460800 write_flash 0x310000 DOOM1_PROCESSED.WAD
```

### 方式三：ESP-IDF 标准烧录

```powershell
# 仅烧固件（不含 WAD）
idf.py -p COM4 flash
```

> 注意：此方式只烧固件，不含关卡数据。需要额外烧 WAD 才能玩游戏。

---

## 验证

烧录完成后，打开串口监视器：

```powershell
idf.py -p COM4 monitor
```

正常启动标志：
1. 显示标题画面（DOOM logo）
2. 底部有 16 行黑色遮挡条
3. 按 OK 键开始游戏
4. 演示模式会自动播放（几秒后进入）

---

## Flash 分区布局

```
0x000000 ┌─────────────────┐
         │  Bootloader     │  21 KB
0x008000 ├─────────────────┤
         │  Partition Table│  3 KB
0x009000 ├─────────────────┤
         │  NVS            │  24 KB
0x00F000 ├─────────────────┤
         │  PHY Init       │  4 KB
0x010000 ├─────────────────┤
         │                 │
         │  Factory App    │  3 MB
         │  (固件)         │
         │                 │
0x310000 ├─────────────────┤
         │                 │
         │  WAD 数据       │  ~4.9 MB
         │  (关卡/贴图/音效)│
         │                 │
0x800000 └─────────────────┘  8 MB Flash
```

---

## 社区发布

### 发布文件清单

- `build/ai-passport-doom-full.bin` — 一键烧录完整镜像
- 封面图 — 项目展示用
- 中英文标题与简介 — 面向公众，不含技术细节

### 发布文案

**标题**：DOOM

**简介**：

GBADoom × AI-Passport 移植项目。把 PrBoom 引擎跑在巴掌大的可穿戴胸牌上，彩色屏幕实时渲染 3D 画面，三键操作即开即玩。

**源代码**：https://github.com/YeatsLiao/ai-passport-doom
