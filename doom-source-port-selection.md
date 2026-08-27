# Doom 引擎选型分析

> 目标硬件：AI-Passport（ESP32-C3，160MHz RISC-V 单核，~400KB SRAM，无 PSRAM，8MB Flash，ST7789P3 240×320 SPI 屏，3 按键）

## 硬件预算分析

| 资源 | 总量 | 系统占用 | Doom 可用 |
|------|------|----------|-----------|
| SRAM | ~400 KB | ~100-150 KB（FreeRTOS + 最小驱动） | **~200-250 KB** |
| Flash | 8 MB | ~3 MB（系统 + app 分区） | **~5 MB**（存 WAD） |
| CPU | 160 MHz 单核 | ~5%（系统任务） | **~152 MHz** |
| 显示 | 240×320 RGB565 SPI@40MHz | — | 直接驱动 |

> 关键约束：**无 PSRAM**，所有数据必须挤进 ~250KB 可用 RAM 或从 Flash 实时流加载。

---

## 候选方案全览

### 方案 A：GBADoom ⭐ 推荐

| 项目 | 详情 |
|------|------|
| 来源 | [kippykip/GBADoom](https://github.com/kippykip/GBADoom)（活跃维护的上游 fork） |
| 原始平台 | Game Boy Advance — ARM7TDMI @ 16.8 MHz |
| RAM 需求 | **~256 KB**（GBA 外部 WRAM）+ 32KB 内部 + 96KB VRAM |
| 基线 | PrBoom 引擎，C 语言 |
| 功能 | 完整 Doom 关卡、怪物 AI、音效、存档、作弊码 |
| 许可证 | GPL |
| 移植先例 | Prusa Core One（STM32F427，256KB RAM，~7 FPS）✅ 2026 年 6 月 |

**优势：**
- 最接近我们硬件约束的已验证方案（256KB ≈ 我们的 ~250KB 可用）
- Prusa 项目证明 GBADoom 可在 STM32（与 ESP32-C3 类似的 MCU）上运行
- 核心引擎是纯 C，便于移植到 ESP-IDF
- 已有成熟的低内存优化（裁剪深度照明、mip-mapping、压缩纹理查找表）

**劣势：**
- 含 GBA 专属代码（ARM 汇编、硬件寄存器访问），需剥离替换
- 渲染输出需重写为 ST7789 SPI 输出
- 音频系统需适配 ES8311 I2S

**预估帧率：** 5-15 FPS（取决于关卡复杂度）

---

### 方案 B：rp2040-doom

| 项目 | 详情 |
|------|------|
| 来源 | [kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom) |
| 原始平台 | Raspberry Pi Pico — 双核 Cortex-M0+ @ 270MHz（超频），264KB SRAM |
| RAM 需求 | **264 KB**（几乎用尽全部） |
| 基线 | Chocolate Doom |
| 功能 | 完整 Doom、VGA 输出、OPL2 音乐、存档、网络多人、Demo 回放、作弊码 |
| 许可证 | GPL-2.0 |
| WAD 格式 | 自研 WHD/WHX 压缩格式 |

**优势：**
- 在比 ESP32-C3 **更少 RAM**（264KB < ~400KB）上成功运行
- 功能最完整的嵌入式 Doom
- 代码质量高，文档完善

**劣势：**
- 深度绑定 pico-sdk，移植到 ESP-IDF 工作量巨大
- 依赖**双核**架构（一核渲染 + 一核 I/O）
- 超频到 270MHz（C3 只有 160MHz）
- WHD/WHX 格式需理解并适配

**预估帧率：** 3-8 FPS（单核劣势明显）

---

### 方案 C：Doomgeneric

| 项目 | 详情 |
|------|------|
| 来源 | [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) |
| 原始平台 | 跨平台（Windows/X11/SDL/Emscripten） |
| RAM 需求 | **默认 4 MB**（可通过 `-DMIN_RAM` 降低） |
| 基线 | 原始 Doom 源码 |
| 功能 | 完整 Doom |
| 许可证 | MIT |
| 移植接口 | 仅 5 个函数（DG_Init / DG_DrawFrame / DG_SleepMs / DG_GetTicksMs / DG_GetKey） |

**优势：**
- 最简移植接口（5 个函数即可跑起来）
- 零历史包袱，代码干净
- 社区活跃，多平台已验证

**劣势：**
- 默认 RAM 需求远超我们预算（4MB vs 250KB）
- 需要大量底层修改才能降到 250KB
- 无现成的低内存优化可参考
- 音频不支持

**预估帧率：** 未优化前无法运行；优化后取决于压缩程度

---

### 方案 D：MG21DOOM

| 项目 | 详情 |
|------|------|
| 来源 | [next-hack/MG21DOOM](https://github.com/next-hack/MG21DOOM) |
| 原始平台 | 宜家智能灯泡 MGM210L — Cortex-M33 @ 80MHz，**108KB RAM** |
| RAM 需求 | **108 KB**（史上最极限！） |
| 基线 | 原始 Doom 源码 + 15 项重大优化 |
| 功能 | 全部关卡、音效（需额外存储） |
| 许可证 | GPL |

**优势：**
- 在**远低于**我们 RAM 的硬件上运行 Doom（108KB!）
- 15 项优化技巧极具参考价值（32→16→8 位数据类型、裁剪枚举、减少纹理路径等）
- 证明 Doom 可以在我们的硬件上运行

**劣势：**
- 深度绑定 EFR32MG21 MCU（ARM Cortex-M33 寄存器、外设）
- 自定义 WAD 格式，需转换工具
- 代码修改极其激进，可读性差
- 无音效（除非加外挂存储）
- 80MHz Cortex-M33 有硬件除法器，ESP32-C3 RISC-V 没有

**预估帧率：** 3-7 FPS

---

### 方案 E：Retro-Go / PrBoom-go ❌ 排除

| 项目 | 详情 |
|------|------|
| 来源 | [ducalex/retro-go](https://github.com/ducalex/retro-go) |
| 原始平台 | ESP32 设备（ODROID-GO 等），**需要 PSRAM** |
| RAM 需求 | **4-8 MB PSRAM** |

**排除原因：** 硬性依赖 PSRAM，ESP32-C3 无 PSRAM，无法适配。

---

### 方案 F：原始 Doom 源码 / Chocolate Doom / PRBoom ❌ 排除

| 项目 | RAM 需求 |
|------|----------|
| 原始 Doom | 4-8 MB |
| Chocolate Doom | 2-4 MB |
| PRBoom | 4-8 MB |

**排除原因：** RAM 需求远超 ESP32-C3 能力。

---

## 综合对比矩阵

| 维度 | GBADoom ⭐ | rp2040-doom | Doomgeneric | MG21DOOM |
|------|-----------|-------------|-------------|----------|
| RAM 匹配度 | ★★★★★ | ★★★★☆ | ★★☆☆☆ | ★★★★★ |
| 代码可移植性 | ★★★★☆ | ★★☆☆☆ | ★★★★★ | ★★☆☆☆ |
| ESP-IDF 适配难度 | ★★★☆☆ | ★☆☆☆☆ | ★★★★★ | ★★☆☆☆ |
| 功能完整度 | ★★★★☆ | ★★★★★ | ★★★★☆ | ★★★☆☆ |
| 社区/文档 | ★★★★☆ | ★★★★★ | ★★★★☆ | ★★★☆☆ |
| 预估帧率 | 5-15 FPS | 3-8 FPS | 未知 | 3-7 FPS |
| 开发工作量 | 中等（2-3 周） | 大（4-6 周） | 大（需自研优化） | 大（3-4 周） |
| **综合推荐度** | **🥇** | **🥉** | **🏅 备选** | **🥈 参考** |

---

## 选型结论

### 主推方案：GBADoom → ESP32-C3 移植

**理由：**
1. **已验证**：Prusa Core One 项目（2026.6）证明 GBADoom 可在 256KB RAM 的 STM32 上跑，与我们的 ESP32-C3 ~250KB 可用 RAM 高度匹配
2. **工作量可控**：核心引擎是纯 C，主要工作是替换平台层（显示/音频/输入）
3. **效果可期**：GBA 原版 ~35 FPS，ESP32-C3 160MHz 预估 5-15 FPS，完全可玩

### 仓库选择：doomhack/GBADoom

| 维度 | [doomhack/GBADoom](https://github.com/doomhack/GBADoom) | [Kippykip/GBADoom](https://github.com/kippykip/GBADoom) |
|------|------|------|
| 角色 | **上游原版**（引擎核心作者） | doomhack 的 fork（UI 增强） |
| Stars | 294 ⭐ | 60 ⭐ |
| 关系 | 原始项目 | fork 自 doomhack |
| 独有改动 | 引擎级优化（内存节省、精灵渲染、mobj 池） | GBA 零售版 HUD、音效修复、武器顺序、Gamma 滑块 |
| 状态 | Kippykip 的改动已**合并回** doomhack | 自 2021.12 起不再更新 |
| 推荐 | ✅ **用这个** | ❌ 已过期 |

**结论：用 [doomhack/GBADoom](https://github.com/doomhack/GBADoom)。** 它是上游原版，Kippykip 的所有改进已经合并回来了，且 Kippykip 自己也推荐回到 doomhack 获取后续引擎更新。

### 技术参考：MG21DOOM 优化技巧

MG21DOOM 的 15 项优化技巧可作为后备方案，如果 GBADoom 的 RAM 仍不够，可以借鉴其：
- 32→16→8 位数据类型降级
- 枚举→宏常量
- 纹理路径裁剪
- Flash 直读（XIP / mmap）替代 RAM 缓存

### 按键映射方案（长按区分模式）

用户已确认方案：**短按 OK = 开枪，长按 OK = 切换为转向模式**

| 模式 | UP 键 | DOWN 键 | OK 键 |
|------|-------|---------|-------|
| **默认（射击模式）** | 前进 | 后退 | 短按=开枪 |
| **转向模式**（长按 OK 进入） | 左转 | 右转 | 短按=退出转向模式 |

实现细节：
- 长按阈值：~500ms
- 进入/退出转向模式时屏幕角落显示图标提示（如 `↰↱` / `↑↓`）
- 射击模式下无 strafe（横移），简化操作
- 开门/使用 = 走到门前按 OK（短按同时兼顾开枪和使用，引擎自动判断）

### WAD 文件方案

- 使用 **DOOM1.WAD**（shareware 版，~4MB，可合法免费分发）
- 存入 Flash 专用分区（修改 partitions.csv 增加 doom 数据分区）
- 可选：参考 rp2040-doom 的 WHD 压缩格式减少 Flash 占用

---

## 下一步

1. 确认选型（推荐方案 A：GBADoom）
2. 克隆 GBADoom 源码并分析可移植部分
3. 搭建 ESP-IDF 工程骨架（基于 ai-passport BSP）
4. 逐步实现平台适配层
