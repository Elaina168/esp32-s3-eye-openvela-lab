#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/videoio.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2)

#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("CP2 FAIL line=%d errno=%d\n", __LINE__, errno); \
    goto out; \
  } \
} while (0)

static int read_frame(int fd, struct v4l2_buffer *buf)
{
  for (int retry = 0; retry < 300; retry++)
    {
      if (ioctl(fd, VIDIOC_DQBUF, (unsigned long)buf) == 0)
        {
          return 0;
        }

      if (errno != EAGAIN)
        {
          return -1;
        }

      usleep(10000);
    }

  errno = ETIMEDOUT;
  return -1;
}

int main(int argc, char **argv)
{
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  struct v4l2_capability caps = {0};
  struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
  struct v4l2_streamparm parm = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
  struct v4l2_requestbuffers req = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_USERPTR,
    .mode = V4L2_BUF_MODE_FIFO,
  };
  struct v4l2_buffer buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_USERPTR,
  };
  struct fb_videoinfo_s screen = {0};
  struct lcddev_area_s area = {
    .stride = FRAME_WIDTH * 2,
  };
  uint8_t *memory[3] = {0};
  unsigned int frames = 0;
  unsigned int changed = 0;
  uint32_t checksum = 0;
  unsigned int report_at = 30;
  double elapsed;
  int duration = argc > 1 ? atoi(argv[1]) : 15;
  int cycles = argc > 2 ? atoi(argv[2]) : 100;
  int lcd = -1;
  int result = 1;
  int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
  struct timespec start;
  struct timespec now;

  CHECK(fd >= 0);
  CHECK(ioctl(fd, VIDIOC_QUERYCAP, (unsigned long)&caps) == 0);
  printf("CP2 sensor=%s\n", caps.driver);

  fmt.fmt.pix.width = FRAME_WIDTH;
  fmt.fmt.pix.height = FRAME_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  CHECK(ioctl(fd, VIDIOC_TRY_FMT, (unsigned long)&fmt) < 0 &&
        errno == EINVAL);
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.width = 160;
  CHECK(ioctl(fd, VIDIOC_TRY_FMT, (unsigned long)&fmt) < 0 &&
        errno == EINVAL);
  fmt.fmt.pix.width = FRAME_WIDTH;
  CHECK(ioctl(fd, VIDIOC_S_FMT, (unsigned long)&fmt) == 0);
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = 30;
  CHECK(ioctl(fd, VIDIOC_S_PARM, (unsigned long)&parm) < 0 &&
        errno == EINVAL);
  parm.parm.capture.timeperframe.numerator = 0;
  CHECK(ioctl(fd, VIDIOC_S_PARM, (unsigned long)&parm) < 0 &&
        errno == EINVAL);
  parm.parm.capture.timeperframe.numerator = 2;
  CHECK(ioctl(fd, VIDIOC_S_PARM, (unsigned long)&parm) == 0);
  printf("CP2 format and interval checks passed\n");

  for (unsigned int i = 0; i < 3; i++)
    {
      memory[i] = malloc(FRAME_SIZE + 64);
      CHECK(memory[i] != NULL);
    }

  req.count = 1;
  CHECK(ioctl(fd, VIDIOC_REQBUFS, (unsigned long)&req) == 0);
  buf.m.userptr = (unsigned long)memory[0];
  buf.length = FRAME_SIZE - 1;
  CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) < 0 && errno == EINVAL);

  buf.length = FRAME_SIZE + 64;
  for (int cycle = 0; cycle < cycles; cycle++)
    {
      memory[0][FRAME_SIZE] = 0xa5;
      CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
      CHECK(ioctl(fd, VIDIOC_STREAMON, (unsigned long)&type) == 0);
      for (int frame = 0; frame < 2; frame++)
        {
          CHECK(read_frame(fd, &buf) == 0);
          CHECK(buf.flags == 0 && buf.bytesused == FRAME_SIZE);
          CHECK(memory[0][FRAME_SIZE] == 0xa5);
          if (frame == 0)
            {
              CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
            }
        }

      CHECK(ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)&type) == 0);
    }

  printf("CP2 FIFO stop/restart cycles=%d, frames=%d\n",
         cycles, cycles * 2);
  req.count = 3;
  req.mode = V4L2_BUF_MODE_RING;
  CHECK(ioctl(fd, VIDIOC_REQBUFS, (unsigned long)&req) == 0);
  for (unsigned int i = 0; i < 3; i++)
    {
      buf.index = i;
      buf.m.userptr = (unsigned long)memory[i];
      buf.length = FRAME_SIZE;
      CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
    }

  if (duration > 0)
    {
      lcd = open("/dev/lcd0", O_RDWR);
      CHECK(lcd >= 0);
      CHECK(ioctl(lcd, LCDDEVIO_GETVIDEOINFO, (unsigned long)&screen) == 0);
      CHECK(screen.xres > 0 && screen.xres <= FRAME_WIDTH);
      CHECK(screen.yres > 0 && screen.yres <= FRAME_HEIGHT);
      area.col_end = screen.xres - 1;
      area.row_end = screen.yres - 1;
    }

  CHECK(ioctl(fd, VIDIOC_STREAMON, (unsigned long)&type) == 0);
  clock_gettime(CLOCK_MONOTONIC, &start);
  now = start;
  printf("CP2 PREVIEW_START seconds=%d LCD=%ux%u\n",
         duration, screen.xres, screen.yres);
  do
    {
      uint32_t next = 0;
      uint8_t *pixels;

      CHECK(read_frame(fd, &buf) == 0);
      CHECK(buf.flags == 0 && buf.bytesused == FRAME_SIZE);
      pixels = (uint8_t *)buf.m.userptr;
      for (unsigned int i = 0; i < FRAME_SIZE; i += 997)
        {
          next = next * 33 + pixels[i];
        }

      if (frames++ != 0 && next != checksum)
        {
          changed++;
        }

      checksum = next;
      if (lcd >= 0)
        {
          area.data = pixels + (FRAME_WIDTH - screen.xres);
          CHECK(ioctl(lcd, LCDDEVIO_PUTAREA, (unsigned long)&area) == 0);
        }

      CHECK(ioctl(fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
      clock_gettime(CLOCK_MONOTONIC, &now);
      elapsed = now.tv_sec - start.tv_sec +
                (now.tv_nsec - start.tv_nsec) / 1e9;
      if (elapsed >= report_at)
        {
          printf("CP2 PREVIEW elapsed=%.3fs frames=%u changed=%u\n",
                 elapsed, frames, changed);
          report_at += 30;
        }
    }
  while (frames < 12 || elapsed < duration);

  CHECK(ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)&type) == 0);
  CHECK(changed > 0);
  printf("CP2 PASS ring frames=%u changed=%u elapsed=%.3fs LCD=%ux%u\n",
         frames, changed,
         elapsed, screen.xres, screen.yres);
  result = 0;

out:
  if (fd >= 0)
    {
      close(fd);
    }

  if (lcd >= 0)
    {
      close(lcd);
    }

  for (unsigned int i = 0; i < 3; i++)
    {
      free(memory[i]);
    }

  return result;
}
