# ESP32-S3-EYE openVela 课程实验

嵌入式操作系统课程期末项目。基于 `dev-ai-contest-2026` 的 ESP32-S3-EYE 板级适配，完成基础正确性验证和两项性能优化。

- 方向 A：摄像头采集、帧缓冲与内存优化。
- 方向 B：LCD 的 SPI DMA 与预览显示优化。

## 当前进度

阶段一为环境准备与基线复现（CP1）。课程基线已在真机上进入 NSH，已留存原版外设观察。Linux 平台源码已同步并锁定，Goldfish ARM64 已完成构建及 NSH 交互验证，板卡丝印待补。D1–D4 的正式定位修复属于后续阶段。

- [CP1 环境检查表](docs/CP1环境检查表.md)
- [实验过程与验收记录](docs/实验记录.md)
- [已核对的基线源码版本](baseline-versions.txt)

完整 Linux 平台清单包含 250 个仓库，见[版本锁定清单](version-lock.txt)和[固定 manifest](manifest-pinned.xml)。`baseline-versions.txt` 保留初次真机构建直接核对的仓库和依赖。
