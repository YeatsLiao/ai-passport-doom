# 问题记录

## 已解决

### 1. Z_Malloc 内存耗尽（红屏卡死）

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

### 2. globals_t 过大导致 zone 碎片化

**现象**：zone 空间被 globals_t 中的大数组（drawsegs、openings、vissprites）占满。

**解决**：将 drawsegs/openings/vissprites 从 `globals_t` 移到 `r_hotpath.iwram.c` 中的静态数组，节省 ~20KB zone 空间。

### 3. 帧缓冲分配失败

**现象**：`Framebuffer alloc failed! backbuffer=0x0`

**根因**：overflow 静态缓冲区太大（.bss 段），挤掉了系统堆空间。

**解决**：
- 帧缓冲（38KB）和行缓冲（8KB）从 `malloc` 改为 `.bss` 静态数组
- overflow 缓冲区保持 128KB
- DOOM 任务栈从 32KB 缩减到 16KB（大数组已移出）

### 4. WiFi 配置无法持久关闭

**现象**：ESP-IDF 构建系统每次 build 会把 `CONFIG_ESP_WIFI_ENABLED` 重新设回 `y`。

**解决**：接受现状——WiFi 编译进固件但不调用 `esp_wifi_init()`，运行时不占额外内存。通过 `sdkconfig.defaults` + `SDKCONFIG_DEFAULTS` 变量确保配置被加载。

### 5. 底部黑条叠加 "AI-PASSPORT-DOOM" 红色文字

**需求**：在底部 16 行黑色遮挡条区域显示红色 "AI-PASSPORT-DOOM" 字样，不频闪。

**尝试过程**：
1. 使用 5x7 点阵字体表 + MSB-first 位读取 → 乱码
2. 改为 LSB-first 位读取 → 仍然乱码
3. 尝试 2x 放大渲染 → 更乱
4. 换字体数据源 → 仍然乱码（字体存储格式与渲染逻辑不匹配）
5. **最终方案**：完全绕过字体表，硬编码每个字符的像素位置直接写入 linebuf → 正常显示

**根因**：所用 5x7 字体数据的存储格式（列主序/行主序/位序）与渲染逻辑不匹配，且难以快速定位具体格式。硬编码方案彻底规避了字体格式问题。

**关键修改文件**：`i_system_esp32.c`（`draw_text_on_blackbar` 函数）

### 6. 演示模式无法通过按键退出

**现象**：游戏启动后如果不操作，自动进入演示模式。演示回放期间按任意键均无响应，必须等演示自然结束才能操作菜单。

**根因**：`G_Responder()` 中 `demoplayback` 期间（非标题画面 `GS_DEMOSCREEN`）直接 `return false`，所有按键事件被忽略，无法中断演示回放。

**解决**：在 `G_Responder()` 的 demo 处理分支中，增加 `demoplayback` 状态的按键处理——任意 `ev_keydown` 设置 `demoplayback = false` 并调用 `M_StartControlPanel()` 打开主菜单。

**关键修改文件**：`GBADoom/source/g_game.c`（`G_Responder` 函数）

---

## 待解决

| 问题 | 状态 | 说明 |
|------|------|------|
| 左侧鬼影 | 面板硬件限制 | ST7789P3 列驱动串扰，轻微不影响游戏 |
| 帧率优化 | 未开始 | 预估 5-15 FPS，可尝试关闭音频/减少渲染行数 |
| 声音系统 | 已禁用 | ESP32-C3 RAM 不够同时跑音频，可考虑 I2S 输出 |
