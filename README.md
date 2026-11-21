# ESP32-S3-EYE openVela 课程实验

嵌入式操作系统课程期末项目。基于 `dev-ai-contest-2026` 的 ESP32-S3-EYE 板级适配，完成基础正确性验证和两项性能优化。

- 方向 A：摄像头采集、帧缓冲与内存优化。
- 方向 B：LCD 的 SPI DMA 与预览显示优化。

## 当前进度

CP1 已完成真机启动、原版外设观察和 Goldfish ARM64 的 NSH 验证。CP2 已修复 D1–D4 及复核发现的帧错位。

CP3 完成双缓冲、采集 DMA 余量调整，以及 SPI DMA 下的带步长批量送屏。最终版通过 100 次采集启停，30 秒真机预览取得 451 帧且无错误帧，组员确认画面完整。CP4 的 xTS 必测项和系统回归待执行，板卡丝印、XCLK 波形和录音证据待补。

- [CP1 环境检查表](docs/CP1环境检查表.md)
- [实验过程与验收记录](docs/实验记录.md)
- [CP2 缺陷修复与复测](docs/CP2缺陷修复与复测.md)
- [CP2 帧同步修复与复测](docs/CP2帧同步修复与复测.md)
- [CP3 纯采集基线](docs/CP3纯采集基线.md)
- [CP3 采集与显示对照](docs/CP3采集与显示对照.md)
- [CP3 后续 A/B 优化](docs/CP3后续AB优化.md)
- [CP3 构建与复测](tests/cp3/README.md)
- [已核对的基线源码版本](baseline-versions.txt)

完整 Linux 平台清单包含 250 个仓库，见[版本锁定清单](version-lock.txt)和[固定 manifest](manifest-pinned.xml)。`baseline-versions.txt` 保留初次真机构建直接核对的仓库和依赖。
