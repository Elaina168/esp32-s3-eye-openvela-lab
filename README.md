# ESP32-S3-EYE openVela 课程实验

嵌入式操作系统课程期末项目。基于 `dev-ai-contest-2026` 的 ESP32-S3-EYE 板级适配，完成基础正确性验证和两项性能优化。

- 方向 A：摄像头采集、帧缓冲与内存优化。
- 方向 B：LCD 的 SPI DMA 与预览显示优化。

## 当前进度

课程基线固件已编译、烧录并在真机上进入 NSH。摄像头取帧、连续预览、D1–D4 复现、A/B 性能测试和 xTS 回归待进行。

- [实验过程与验收记录](docs/实验记录.md)
- [已核对的基线源码版本](baseline-versions.txt)

`baseline-versions.txt` 记录当前构建所用的关键仓库和依赖，完整 `repo` 版本锁定清单待同步后生成。
