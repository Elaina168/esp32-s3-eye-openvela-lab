#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/videoio.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/arch.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>

extern uint64_t esp32s3_rt_timer_time_us(void);
extern void cp3_cam_probe_read(uint64_t *frames, uint64_t *cache_us,
                              uint64_t *copy_us);

#define W 320
#define H 240
#define SIZE (W * H * 2)
#define LCD_W 240
#define LAST_MS 300
#define CHECK(c) do { if (!(c)) { printf("AB FAIL line=%d errno=%d\n", __LINE__, errno); goto out; } } while (0)

static int64_t read_clock(clockid_t id)
{
  if (id == CLOCK_MONOTONIC)
    return esp32s3_rt_timer_time_us();

  struct timespec ts;
  if (clock_gettime(id, &ts) < 0)
    return -1;
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(int argc, char **argv)
{
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
  struct v4l2_requestbuffers req = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_USERPTR,
    .mode = V4L2_BUF_MODE_RING,
  };
  struct v4l2_buffer buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_USERPTR,
    .length = SIZE,
  };
  struct lcddev_area_s area = {.row_end = 239, .col_end = 239};
  struct fb_videoinfo_s screen;
  struct pollfd ready = {.fd = -1, .events = POLLIN};
  uint16_t *memory[3] = {0};
  unsigned int hist[LAST_MS + 1] = {0};
  unsigned int valid = 0;
  unsigned int bad = 0;
  unsigned int changed = 0;
  unsigned int p95 = 0;
  unsigned int accum = 0;
  unsigned int heap_before = mallinfo().uordblks;
  unsigned int heap_active = 0;
  unsigned int payload = 0;
  uint32_t previous = 0;
  int64_t start;
  int64_t measured = 0;
  int64_t now;
  int64_t cpu_start = -1;
  int64_t cpu_end;
  uint64_t measured_frames = 0;
  uint64_t measured_cache = 0;
  uint64_t measured_copy = 0;
  int64_t sum_crop = 0;
  int64_t sum_lcd = 0;
  int64_t sum_path = 0;
  int64_t max_lcd = 0;
  int64_t elapsed;
  int fd = -1;
  int lcd = -1;
  int rc = 1;
  const char *mode = argc >= 2 ? argv[1] : "lcd";
  int seconds = argc >= 3 ? atoi(argv[2]) : 12;
  int count = argc >= 4 ? atoi(argv[3]) : 3;
  bool display = strcmp(mode, "capture") != 0;
  bool camera = strcmp(mode, "lcd") != 0;
#ifdef CONFIG_ESP32S3_SPI_DMA
  bool dma = true;
#else
  bool dma = false;
#endif

  /* 两个对照镜像都用板级固定 CPU 频率换算任务计数。 */

  up_perf_init((void *)(uintptr_t)
               (CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ * 1000000UL));
  CHECK(!strcmp(mode, "lcd") || !strcmp(mode, "preview") ||
        !strcmp(mode, "capture"));
  CHECK(seconds >= 5 && seconds <= 120 && count >= 2 && count <= 3);
  if (display)
    {
      lcd = open("/dev/lcd0", O_RDWR);
      CHECK(lcd >= 0);
      CHECK(ioctl(lcd, LCDDEVIO_GETVIDEOINFO, (unsigned long)&screen) == 0);
      CHECK(screen.xres == LCD_W && screen.yres == H);
    }

  if (camera)
    {
      fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
      CHECK(fd >= 0);
      ready.fd = fd;
      fmt.fmt.pix.width = W;
      fmt.fmt.pix.height = H;
      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
      CHECK(ioctl(fd, VIDIOC_S_FMT, (unsigned long)&fmt) == 0);
      req.count = count;
      CHECK(ioctl(fd, VIDIOC_REQBUFS, (unsigned long)&req) == 0);
      CHECK(req.count == count);
    }

  for (int i = 0; i < (camera ? count : 1); i++)
    {
      memory[i] = malloc(SIZE);
      CHECK(memory[i] != NULL);
      payload += SIZE;
      if (camera)
        {
          buf.index = i;
          buf.m.userptr = (unsigned long)memory[i];
          CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
        }
    }

  if (camera)
    {
      CHECK(ioctl(fd, VIDIOC_STREAMON, (unsigned long)&type) == 0);
    }
  else
    {
      const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff};
      for (unsigned int y = 0; y < H; y++)
        for (unsigned int x = 0; x < W; x++)
          memory[0][y * W + x] = y == 119 || y == 120 ||
            x == 159 || x == 160 ? 0 : colors[2 * (y >= 120) + (x >= 160)];
    }

  if (dma && !camera)
    {
      for (unsigned int y = 0; y < H; y++)
        memmove(memory[0] + y * LCD_W, memory[0] + y * W + 40, LCD_W * 2);
    }

  printf("AB START mode=%s dma=%d buffers=%d seconds=%d\n",
         mode, dma, camera ? count : 0, seconds);
  start = read_clock(CLOCK_MONOTONIC);
  do
    {
      uint16_t *pixels = memory[0];
      uint32_t checksum = 0;
      int64_t draw_us = 0;
      int64_t crop_us = 0;
      int64_t path_us = 0;
      bool good = true;
      if (camera)
        {
          CHECK(poll(&ready, 1, 1500) == 1 && (ready.revents & POLLIN));
          CHECK(ioctl(fd, VIDIOC_DQBUF, (unsigned long)&buf) == 0);
          good = buf.flags == 0 && buf.bytesused == SIZE;
          pixels = (uint16_t *)buf.m.userptr;
          if (good)
            for (unsigned i = 0; i < SIZE / 2; i += 499)
              checksum = checksum * 33 + pixels[i];
        }

      if (display && good)
        {
          int64_t draw_start = read_clock(CLOCK_MONOTONIC);
          /* 居中裁剪，缓冲区在显示调用返回后才重新入队。 */
          if (dma)
            {
              if (camera)
                {
                  for (unsigned int y = 0; y < H; y++)
                    memmove(pixels + y * LCD_W, pixels + y * W + 40,
                            LCD_W * 2);
                }

              area.data = (uint8_t *)pixels;
              area.stride = LCD_W * 2;
            }
          else
            {
              area.data = (uint8_t *)(pixels + 40);
              area.stride = W * 2;
            }

          crop_us = read_clock(CLOCK_MONOTONIC) - draw_start;
          CHECK(ioctl(lcd, LCDDEVIO_PUTAREA, (unsigned long)&area) == 0);
          draw_us = read_clock(CLOCK_MONOTONIC) - draw_start;
        }

      if (camera && good)
        {
          path_us = (int64_t)esp32s3_rt_timer_time_us() -
                    ((int64_t)buf.timestamp.tv_sec * 1000000 +
                     buf.timestamp.tv_usec);
          CHECK(path_us >= 0);
        }

      if (camera)
        CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);

      now = read_clock(CLOCK_MONOTONIC);
      if (measured == 0 && now - start >= 2000000)
        {
          heap_active = mallinfo().uordblks;
          usleep(1000);
          cpu_start = read_clock(CLOCK_PROCESS_CPUTIME_ID);
          measured = read_clock(CLOCK_MONOTONIC);
          if (camera)
            cp3_cam_probe_read(&measured_frames, &measured_cache,
                               &measured_copy);
        }
      else if (measured != 0)
        {
          if (good)
            {
              if (valid != 0 && checksum != previous)
                changed++;
              previous = checksum;
              valid++;
              sum_lcd += draw_us;
              sum_crop += crop_us;
              sum_path += path_us;
              if (draw_us > max_lcd)
                max_lcd = draw_us;
              int64_t bucket = (camera ? path_us : draw_us) / 1000;
              hist[bucket < LAST_MS ? bucket : LAST_MS]++;
            }
          else
            bad++;
        }
    }
  while (measured == 0 || now - measured < (int64_t)seconds * 1000000);

  elapsed = now - measured;
  usleep(1000);
  cpu_end = read_clock(CLOCK_PROCESS_CPUTIME_ID);
  CHECK(valid);
  for (p95 = 0; p95 < LAST_MS; p95++)
    {
      accum += hist[p95];
      if (accum >= (valid * 95 + 99) / 100)
        break;
    }

  if (camera)
    CHECK(ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)&type) == 0);
  printf("AB RESULT mode=%s dma=%d buffers=%d wall_us=%lld valid=%u bad=%u "
         "changed=%u fps=%.3f lcd_mean_us=%lld lcd_max_us=%lld "
         "path_mean_us=%lld p95_ms_bucket=%u cpu_us=%lld "
         "heap_delta=%d payload=%u\n",
         mode, dma, camera ? count : 0, (long long)elapsed, valid, bad,
         changed, valid * 1000000.0 / elapsed, (long long)(sum_lcd / valid),
         (long long)max_lcd, (long long)(sum_path / valid), p95,
         (long long)(cpu_start >= 0 && cpu_end >= cpu_start ?
                     cpu_end - cpu_start : -1),
         (int)heap_active - (int)heap_before, payload);
  printf("AB DETAIL crop_mean_us=%lld lcd_submit_mean_us=%lld\n",
         (long long)(sum_crop / valid),
         (long long)((sum_lcd - sum_crop) / valid));
  if (camera)
    {
      uint64_t frames;
      uint64_t cache;
      uint64_t copy;
      cp3_cam_probe_read(&frames, &cache, &copy);
      printf("AB CAM_PROFILE frames=%llu cache_total_us=%llu copy_total_us=%llu\n",
             (unsigned long long)(frames - measured_frames),
             (unsigned long long)(cache - measured_cache),
             (unsigned long long)(copy - measured_copy));
    }

  rc = 0;
out:
  if (fd >= 0)
    close(fd);
  if (lcd >= 0)
    close(lcd);
  for (int i = 0; i < 3; i++)
    free(memory[i]);
  return rc;
}
