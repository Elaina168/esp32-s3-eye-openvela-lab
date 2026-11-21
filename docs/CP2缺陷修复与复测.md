# CP2 缺陷修复与复测

本文保留首轮 CP2 复测记录。后续发现原始帧错位，当前验收结果及镜像校验值见[帧同步修复与复测](CP2帧同步修复与复测.md)。下方复现命令使用仓库当前的补丁和复测程序。

在 ESP32-S3-EYE 真机上验证课程手册 4.1.2 的 D1–D4。源码版本以[锁定清单](../version-lock.txt)为准。CP1 原版运行 `camera 1` 后等待 30.048 秒仍未返回 NSH，日志不足以判断当时是否收到首帧。CP2 对照镜像与首次 CP1 镜像分别归档。

| 缺陷 | 源码根因 | 修复 |
| --- | --- | --- |
| D1 时钟与麦克风引脚冲突 | 板级调用 LEDC 通道 0，默认 GPIO2，日志硬编码为 GPIO15。I2S0 的 DIN 也在 GPIO2 | 移除 EYE 的 LEDC 初始化和板级 LEDC 配置。保留 LCD_CAM 在 GPIO15 上的 XCLK，分频配置对应 24 MHz。GPIO2 由 I2S0 RX 使用 |
| D2 连续采集 | VSYNC 帧结束停止 CAM 和 DMA，工作队列交帧后没有续采 | 单独维护流状态和当前帧硬件状态。工作队列校验并交付帧，只有 V4L2 回调交回可写缓冲且流仍开启时才重建 DMA 链并启动下一帧。FIFO 无空闲缓冲时暂停，重新排队后继续。停止时关闭 CAM 和 DMA 并取消或等待已提交的工作 |
| D3 参数和传感器检查 | OV2640 的可用性和参数检查恒成功 | SCCB 在 sensor bank 读 PID/VER，核对 `26:42`，探测失败阻止注册。传感器仅接受单路 QVGA RGB565 和等价于 1/15 秒的非零帧间隔。普通 `camera 1` 只准备 RGB565 视频流，显式 `-jpg` 请求仍走 JPEG 格式校验 |
| D4 DMA 描述符容量 | 固定 56 个描述符，未约束总需求，超出容量时链尾会指向数组外。用户缓冲容量也被当成帧长度 | 按格式计算帧长，再检查用户容量。分配前检查描述符总容量并对齐整块 DMA 缓冲，链尾始终为 `NULL`。DMA 未停稳或完整接收字节数不足时，返回错误帧，跳过数据拷贝 |

## 首轮真机复测

| 项目 | 操作与结果 |
| --- | --- |
| D1 | 有效配置确认 LEDC 关闭、CAM XCLK 为 GPIO15、I2S0 DIN 为 GPIO2。真机能读 OV2640 ID 并持续取帧。GPIO15 的频率为寄存器分频推导，尚未用示波器测量。麦克风录音未测 |
| D2 FIFO | 单缓冲模式连续 100 次 `STREAMON` → 两次有效取帧 → `STREAMOFF`。两帧之间由 `QBUF` 恢复，累计 200 帧 |
| D2 LCD 预览 | 三缓冲 RING 模式向 `/dev/lcd0` 提交 240×240 画面。一次 60.010 秒测试取到 897 帧，相邻帧抽样校验值变化 896 次，正常停止并返回 NSH。组员观察到板载画面随镜头前动作变化 |
| D3 | 启动和打开设备时读到 `26:42`，`VIDIOC_QUERYCAP` 返回 `OV2640`。拒绝 JPEG、160×240、1/30 秒和零分子，接受 320×240 RGB565 与等价的 2/30 秒间隔。器件缺失路径已核对源码 |
| D4 | 比帧长少 1 字节的缓冲被 `QBUF` 拒绝；比帧长多 64 字节的缓冲每帧返回 153,600 字节，帧后哨兵保持原值。用实际描述符构造及格式校验函数做主机边界测试，198,144 字节由 52 个描述符完整覆盖，215,040 字节由 56 个完整覆盖，两个链尾均为 `NULL`。超出容量的格式在主机测试中返回 `E2BIG` |

首轮联测完成了 100 次启停，随后 USB 从 Ubuntu 断开，导致该轮预览结果未能通过串口取回。重新连接 USB 后单独重跑定时预览并读到结束统计。测试固件使用 8 KiB 的诊断任务栈，正式镜像恢复课程示例及默认栈配置。

正式固件烧录后连续两次执行原版曾阻塞的 `camera 1`。两次分别在 15.393 秒、15.400 秒返回 NSH，并在临时 RAM 盘生成 `VIDEO001.RGB` 和 `VIDEO002.RGB`，各 153,600 字节。验证后卸载 RAM 盘。

| 归档镜像 | SHA-256 |
| --- | --- |
| 正式固件，1,617,532 字节 | `02d1b70bc8425fe75ddc162b66283f512655a03c81cd4f3cc85f278307a57f5b` |
| 专项测试固件，1,616,772 字节 | `34460647f7bd5742d86e87e3305e03bc735f72648dae936a5318b706eb03867d` |

## 复现

在已同步并锁定的 openVela 源码根目录执行。将 `course_repo` 设为课程仓库的绝对路径。

```sh
git -C nuttx apply "$course_repo/patches/cp2/nuttx.patch"
git -C apps apply "$course_repo/patches/cp2/apps.patch"
git -C vendor/espressif apply "$course_repo/patches/cp2/espressif.patch"
```

使用 EYE 的 `configs/openvela` 及课程预编译 Xtensa 工具链。Espressif HAL 使用 `9fc713a95b1ff150dd0b0647e465d3c624056bb1`。若该 HAL 尚未包含课程构建兼容修改，在 HAL 工作树应用[构建补丁](../patches/cp2/hal-course-build.patch)，其中两处 NuttX 自旋锁初始化使用 `SP_UNLOCKED`。已配置的工作树可以直接从源码根目录执行以下命令。

```sh
make -C nuttx EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j6
```

正式镜像可在 NSH 执行 `camera 1`。要重跑专项测试，在临时测试构建中用 [cp2-check.c](../tests/cp2-check.c) 替换 `apps/examples/camera/camera_main.c`，将其 Makefile 的 `STACKSIZE` 改为 8192 并重新注册应用。

```sh
make -C apps/examples/camera TOPDIR="$PWD/nuttx" APPDIR="$PWD/apps" register
make -C nuttx EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j6
```

烧录后在 NSH 执行 `camera 0 100` 可重跑 FIFO 启停及无 LCD 的短时采集。执行 `camera 60 1` 可重跑计时预览。结束后恢复正式应用源码和 Makefile，再重新注册应用并构建正式镜像。

长时间预览、XCLK 示波器波形、录音和 PCB 丝印照片待补。A/B 性能测量和 xTS 回归属于后续阶段。
