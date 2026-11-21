# ESP32-S3-EYE openVela 课程实验

嵌入式操作系统课程期末项目。基于 `dev-ai-contest-2026` 的 ESP32-S3-EYE 板级适配，完成基础正确性验证和两项性能优化。

- 方向 A：摄像头采集、帧缓冲与内存优化。
- 方向 B：LCD 的 SPI DMA 与预览显示优化。

## 当前进度

CP1 已完成真机启动、原版外设观察和 Goldfish ARM64 的 NSH 验证。CP2 已实施 D1–D4 修复，真机通过 100 次采集启停及 60 秒 LCD 预览。板卡丝印、XCLK 波形和录音证据待补。A/B 性能优化将在此正确性基线上开展。

- [CP1 环境检查表](docs/CP1环境检查表.md)
- [实验过程与验收记录](docs/实验记录.md)
- [CP2 缺陷修复与复测](docs/CP2缺陷修复与复测.md)
- [已核对的基线源码版本](baseline-versions.txt)

完整 Linux 平台清单包含 250 个仓库，见[版本锁定清单](version-lock.txt)和[固定 manifest](manifest-pinned.xml)。`baseline-versions.txt` 保留初次真机构建直接核对的仓库和依赖。
