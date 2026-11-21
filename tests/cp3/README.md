# CP3 构建与复测

## 正式构建

从版本锁定清单对应的干净 openVela 工作树开始。把 `course_repo` 设为本课程仓库路径，在 openVela 根目录逐个应用补丁。

```sh
for stage in cp2 cp3/round1 cp3/round2; do
  for repo in nuttx apps vendor/espressif; do
    patch="$course_repo/patches/$stage/${repo##*/}.patch"
    if test -f "$patch"; then
      git -C "$repo" apply --check "$patch" &&
        git -C "$repo" apply "$patch" || exit 1
    fi
  done
done
```

按 [CP2 构建方法](../../docs/CP2缺陷修复与复测.md#复现)选用 ESP32-S3-EYE 的 `openvela` 配置、工具链与 HAL 构建补丁。第一轮代码应用至 `cp3/round1`，最终版应用至 `cp3/round2`。最终配置中 `ESP32S3_SPI_DMA=y`、`EXAMPLES_CAMERA_VIDEO_BUFFERS=2`。

## 专项测试

临时替换 `apps/examples/camera/camera_main.c`，把同目录 Makefile 的 `STACKSIZE` 设为 8192。对 CPU 占用做对照的两个镜像使用相同配置，启用 `SCHED_CRITMONITOR`、`ARCH_PERF_EVENTS` 并设 `SCHED_CRITMONITOR_MAXTIME_THREAD=0`。有 SPI DMA 的测试镜像使用 `ESP32S3_SPI_DMA_BUFSIZE=2048` 和 `ESP32S3_SPI_DMATHRESHOLD=64`。

```sh
make -C apps/examples/camera TOPDIR="$PWD/nuttx" APPDIR="$PWD/apps" register
make -C nuttx EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j6
```

| 对照组 | 代码与配置 | 测试入口 |
| --- | --- | --- |
| 纯采集基线 | CP2 | [capture-bench.c](capture-bench.c)，运行 `camera 20` 三次 |
| 第一轮轮询 | CP2，SPI DMA 关闭，启用测试计时配置 | [ab-bench-poll.c](ab-bench-poll.c)，运行 `camera lcd 12`、`camera capture 12 2`、`camera preview 12 3`、`camera preview 12 2` |
| 第一轮 DMA | 第一轮合并的 SPI 路径，启用测试计时配置 | [ab-bench-dma.c](ab-bench-dma.c)，运行 `camera lcd 12`、`camera preview 12 3`、`camera preview 12 2` |
| 第二轮的直接对照 | 第一轮 A+B，启用测试计时配置及下方相机微秒计时插桩 | [bench-base.c](bench-base.c)，运行 `camera lcd 8`、`camera capture 10 2`、`camera preview 10 2` |
| 第二轮只改 B | 第一轮 A+B 加第二轮带步长发送，启用同一套测试计时 | [bench-fused.c](bench-fused.c)，运行 `camera lcd 8`、`camera preview 10 2` |
| 第二轮 A+B | 最终版，启用同一套测试计时 | [bench-fused.c](bench-fused.c)，运行 `camera capture 10 2`、`camera preview 12 2`、`camera preview 30 2` |

构建第二轮只改 B 的对照时，对 `round2/nuttx.patch` 使用 `git -C nuttx apply --exclude='arch/xtensa/src/esp32s3/esp32s3_cam.c'`，同时应用第二轮 `vendor/espressif` 补丁。在同一工作树补上 A 时，用 `git -C nuttx apply --include='arch/xtensa/src/esp32s3/esp32s3_cam.c'` 应用余下部分。

第二轮测量在每个对照版本上都需要临时插桩。先保存驱动文件和应用配置，在对应版本上运行如下命令，再按上表构建。

```sh
python3 "$course_repo/tests/cp3/instrument-camera.py" \
  nuttx/arch/xtensa/src/esp32s3/esp32s3_cam.c
```

切换对照版本前恢复插桩文件。实验结束后恢复正式应用入口、Makefile 和配置，再构建正式固件。最终版的 100 次启停回归复用 [CP2 专项程序](../cp2-check.c)，在最终驱动上执行 `camera 15 100`。

[第一轮数据](../../docs/data/cp3/round1.csv) · [第二轮数据](../../docs/data/cp3/round2.csv)
