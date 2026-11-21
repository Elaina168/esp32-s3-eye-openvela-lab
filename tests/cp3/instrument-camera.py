from pathlib import Path
import sys
p=Path(sys.argv[1]); s=p.read_text()
s=s.replace('#include "esp32s3_gpio.h"', '#include "esp32s3_gpio.h"\n#include "esp32s3_rt_timer.h"',1)
at=s.index('static void frame_complete_worker(void *arg)\n{')
s=s[:at]+'''static uint64_t g_cp3_frames;
static uint64_t g_cp3_cache_us;
static uint64_t g_cp3_copy_us;

void cp3_cam_probe_read(uint64_t *frames, uint64_t *cache_us,
                        uint64_t *copy_us)
{
  irqstate_t flags = spin_lock_irqsave(&g_cam_priv.lock);
  *frames = g_cp3_frames;
  *cache_us = g_cp3_cache_us;
  *copy_us = g_cp3_copy_us;
  spin_unlock_irqrestore(&g_cam_priv.lock, flags);
}

'''+s[at:]
s=s.replace('      up_invalidate_dcache((uintptr_t)priv->fb,', '''      uint64_t start_us = esp32s3_rt_timer_time_us();
      up_invalidate_dcache((uintptr_t)priv->fb,''',1)
s=s.replace('      memcpy(dest, priv->fb, size);','''      uint64_t cache_end_us = esp32s3_rt_timer_time_us();
      memcpy(dest, priv->fb, size);
      uint64_t copy_end_us = esp32s3_rt_timer_time_us();
      flags = spin_lock_irqsave(&priv->lock);
      g_cp3_frames++;
      g_cp3_cache_us += cache_end_us - start_us;
      g_cp3_copy_us += copy_end_us - cache_end_us;
      spin_unlock_irqrestore(&priv->lock, flags);''',1)
s=s.replace('          gettimeofday(&priv->frame_ts, NULL);','''          uint64_t ts_us = esp32s3_rt_timer_time_us();
          priv->frame_ts.tv_sec = ts_us / 1000000;
          priv->frame_ts.tv_usec = ts_us % 1000000;''',1)
p.write_text(s)
