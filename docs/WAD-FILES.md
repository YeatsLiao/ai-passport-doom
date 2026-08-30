# WAD 文件说明

本项目涉及 4 个 WAD 文件，它们是按顺序生成的管线产物。

## 文件一览

| 文件 | 大小 | 说明 | 来源 |
|------|------|------|------|
| `DOOM1.WAD` | ~4.0 MB | **原版** DOOM Shareware（E1M1-E1M9） | 从 [id Software 官方](https://doomwiki.org/wiki/Shareware) 下载 |
| `DOOM1_GBA.WAD` | ~4.1 MB | **GBA 补丁版**：原版 + gbadoom.wad 补丁合并 | `merge_pwad.py` 生成 |
| `DOOM1_GBA_CHECK.WAD` | ~4.1 MB | **校验副本**：与 DOOM1_GBA.WAD 相同，用于比对验证 | `merge_pwad.py` 生成 |
| `DOOM1_PROCESSED.WAD` | ~3.7 MB | **最终烧录版**：经 GbaWadUtil 格式转换 | `GbaWadUtil.exe` 生成 |

## 为什么不能直接烧 DOOM1.WAD？

GBADoom 引擎对 WAD 内部数据格式做了两处修改：

1. **gbadoom.wad 补丁**：添加了 GBA 专属资源（如状态栏图形 `STGANUM0` 等），不合并会报 `W_GetNumForName: STGANUM0 not found`
2. **seg_t 结构体扩展**：原版 12 字节 → GBA 版 28 字节，不转换会报 `P_GroupLines: Subsector a part of no sector!`

## 管线流程

```
DOOM1.WAD (原版 Shareware)
    │
    │  python tools/merge_pwad.py DOOM1.WAD ../GBADoom/GbaWadUtil/gbadoom.wad DOOM1_GBA.WAD
    │  （合并 GBA 补丁 + 大写化 PNAMES 修复）
    ▼
DOOM1_GBA.WAD (GBA 补丁版)
    │
    │  GbaWadUtil.exe -in DOOM1_GBA.WAD -out DOOM1_PROCESSED.WAD
    │  （seg_t 12→28 字节格式转换）
    ▼
DOOM1_PROCESSED.WAD ← 烧录这个！
```

## 烧录命令

```powershell
# 烧录最终 WAD 到 Flash 分区（offset 0x310000）
esptool.py -p COM4 -b 460800 write_flash 0x310000 DOOM1_PROCESSED.WAD
```

## 各文件用途

- **DOOM1.WAD**：源文件，保留用于重新生成后续版本
- **DOOM1_GBA.WAD**：中间产物，可用于排查合并是否正确
- **DOOM1_GBA_CHECK.WAD**：校验用，确认合并结果一致后可删除
- **DOOM1_PROCESSED.WAD**：**唯一需要烧录的文件**，存入 Flash `wad` 分区，引擎通过 mmap 直接读取

> 所有 WAD 文件均为二进制大文件，已加入 `.gitignore`，不纳入版本控制。
