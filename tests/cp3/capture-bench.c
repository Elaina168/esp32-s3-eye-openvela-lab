#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/videoio.h>
#include <time.h>
#include <unistd.h>

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define FRAME_BYTES (FRAME_WIDTH * FRAME_HEIGHT * 2)
#define BUFFER_COUNT 3
#define WARMUP_US 2000000
#define LAST_LAT_MS 256

#define CHECK(condition) do { \
  if (!(condition)) { \
    printf("CP3 FAIL line=%d errno=%d\n", __LINE__, errno); \
    goto out; \
  } \
} while (0)

static int64_t monotonic_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(int argc, char **argv)
{
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
  struct v4l2_streamparm parm = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
  struct v4l2_requestbuffers req = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_USERPTR,
    .mode = V4L2_BUF_MODE_RING,
    .count = BUFFER_COUNT,
  };
  struct v4l2_buffer buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_USERPTR,
  };
  struct timeval dequeue_time;
  struct pollfd ready = {.fd = -1, .events = POLLIN};
  void *buffers[BUFFER_COUNT] = {0};
  unsigned int hist[LAST_LAT_MS + 1] = {0};
  unsigned int good = 0;
  unsigned int bad = 0;
  unsigned int samples = 0;
  unsigned int races = 0;
  unsigned int p95_ms = 0;
  unsigned int accumulated = 0;
  unsigned int heap_before;
  unsigned int heap_open;
  unsigned int heap_queued;
  unsigned int heap_stream = 0;
  unsigned int heap_end;
  int64_t start;
  int64_t measured_start = 0;
  int64_t now;
  int64_t first_us = -1;
  int64_t sum_us = 0;
  int64_t max_us = 0;
  unsigned int target_s = 20;
  int result = 1;
  bool streaming = false;

  if (argc == 2)
    {
      char *end;
      unsigned long value = strtoul(argv[1], &end, 10);
      if (end == argv[1] || *end != '\0' || value < 5 || value > 120)
        {
          puts("Usage: camera [seconds: 5..120]");
          return 1;
        }

      target_s = (unsigned int)value;
    }
  else if (argc > 2)
    {
      puts("Usage: camera [seconds: 5..120]");
      return 1;
    }

  heap_before = mallinfo().uordblks;
  ready.fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
  CHECK(ready.fd >= 0);
  heap_open = mallinfo().uordblks;
  fmt.fmt.pix.width = FRAME_WIDTH;
  fmt.fmt.pix.height = FRAME_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  CHECK(ioctl(ready.fd, VIDIOC_S_FMT, (unsigned long)&fmt) == 0);
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = 15;
  CHECK(ioctl(ready.fd, VIDIOC_S_PARM, (unsigned long)&parm) == 0);
  CHECK(ioctl(ready.fd, VIDIOC_REQBUFS, (unsigned long)&req) == 0);
  CHECK(req.count == BUFFER_COUNT);

  for (unsigned int i = 0; i < BUFFER_COUNT; i++)
    {
      buffers[i] = malloc(FRAME_BYTES);
      CHECK(buffers[i] != NULL);
      buf.index = i;
      buf.length = FRAME_BYTES;
      buf.m.userptr = (unsigned long)buffers[i];
      CHECK(ioctl(ready.fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
    }

  heap_queued = mallinfo().uordblks;
  start = monotonic_us();
  CHECK(ioctl(ready.fd, VIDIOC_STREAMON, (unsigned long)&type) == 0);
  streaming = true;
  for (;;)
    {
      int ret = poll(&ready, 1, 1000);
      CHECK(ret == 1 && (ready.revents & POLLIN));
      ret = ioctl(ready.fd, VIDIOC_DQBUF, (unsigned long)&buf);
      if (ret < 0 && errno == EAGAIN)
        {
          races++;
          CHECK(monotonic_us() - start <
                (int64_t)(target_s + 5) * 1000000);
          continue;
        }

      CHECK(ret == 0);
      gettimeofday(&dequeue_time, NULL);
      now = monotonic_us();
      bool valid = !(buf.flags & V4L2_BUF_FLAG_ERROR) &&
                   buf.bytesused == FRAME_BYTES;
      if (first_us < 0 && valid)
        {
          first_us = now - start;
        }

      if (measured_start == 0 && now - start >= WARMUP_US)
        {
          measured_start = now;
          heap_stream = mallinfo().uordblks;
        }
      else if (measured_start != 0)
        {
          if (valid)
            {
              good++;

              /* 驱动在帧结束中断中写入同一时钟域的 timeval。 */

              int64_t latency =
                (int64_t)(dequeue_time.tv_sec - buf.timestamp.tv_sec) *
                1000000 + dequeue_time.tv_usec - buf.timestamp.tv_usec;
              if (latency >= 0)
                {
                  unsigned int bucket = latency / 1000 < LAST_LAT_MS ?
                                        latency / 1000 : LAST_LAT_MS;
                  hist[bucket]++;
                  samples++;
                  sum_us += latency;
                  if (latency > max_us)
                    {
                      max_us = latency;
                    }
                }
            }
          else
            {
              bad++;
            }
        }

      CHECK(ioctl(ready.fd, VIDIOC_QBUF, (unsigned long)&buf) == 0);
      if (measured_start != 0 &&
          now - measured_start >= (int64_t)target_s * 1000000)
        {
          break;
        }
    }

  heap_end = mallinfo().uordblks;
  CHECK(ioctl(ready.fd, VIDIOC_STREAMOFF, (unsigned long)&type) == 0);
  streaming = false;
  CHECK(samples > 0 && first_us >= 0);
  for (p95_ms = 0; p95_ms < LAST_LAT_MS; p95_ms++)
    {
      accumulated += hist[p95_ms];
      if (accumulated >= (samples * 95 + 99) / 100)
        {
          break;
        }
    }

  printf("CP3 CAPTURE elapsed_us=%" PRId64 " good=%u bad=%u fps=%.3f"
         " first_us=%" PRId64 " attempts_eagain=%u\n",
         now - measured_start, good, bad,
         good * 1000000.0 / (now - measured_start), first_us, races);
  printf("CP3 LATENCY samples=%u mean_us=%" PRId64
         " p95_ms_bucket=%u max_us=%" PRId64 "\n",
         samples, sum_us / samples, p95_ms, max_us);
  printf("CP3 HEAP before=%u open=%u queued=%u streaming=%u end=%u"
         " user_payload=%u\n",
         heap_before, heap_open, heap_queued, heap_stream, heap_end,
         BUFFER_COUNT * FRAME_BYTES);
  result = 0;

out:
  if (streaming)
    {
      if (ioctl(ready.fd, VIDIOC_STREAMOFF, (unsigned long)&type) < 0)
        {
          fprintf(stderr, "CP3 STREAMOFF errno=%d\n", errno);
        }
    }

  if (ready.fd >= 0)
    {
      close(ready.fd);
    }

  for (unsigned int i = 0; i < BUFFER_COUNT; i++)
    {
      free(buffers[i]);
    }

  return result;
}
