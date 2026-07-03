// Copyright (c) 2023, Tencent Inc.
// All rights reserved.
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "wmpf/cloudvoip_server.h"
#include "wmpf/hardware/video.h"
#include "wmpf/wmpf.h"

#include "list.h"

static list_declare(video_stream_list);
extern wx_cloudvoip_session_status_t voip_status;

typedef struct {
  struct wx_video_stream_config config;
  const struct wx_video_stream_config* listener;
  struct wx_video_stream* stream;
  struct listnode slist;
} VIDEO_STREAM;

static int video_fd = 0;

int voip_receive_video = 0;

static int video_frames = 0;
static int64_t receive_video_stream_ts = 0;
static int64_t video_total_len = 0;

extern int64_t get_timestamp_us(void);

extern int h264_decode_sps(unsigned char * buf, unsigned int nLen, int *width, int *height, int *fps);

static int save_stream = 0;

static void h264_sps_paser(unsigned char *pos, size_t buffer_size) {
  size_t i = 0;
  
  for (i = 0; i < buffer_size - 4; i++) {
    if (pos[i] == 0 && pos[i + 1] == 0 && pos[i + 2] == 1) {
      uint8_t nal_type = pos[i + 3] & 0x1F;

      if (nal_type == 7) {
        int width = 0;
        int height = 0;
        int fps = 0;

        h264_decode_sps(pos + 3, buffer_size - 3, &width, &height, &fps);

        if (width == 1072) {
          save_stream = 1;
        }
      }
      if (nal_type != 7 && nal_type != 8) { // sps  pps
        video_frames++;
      }
      i += 3;
    }
    else if (pos[i] == 0 && pos[i + 1] == 0 && pos[i + 2] == 0 && pos[i + 3] == 1) {
      uint8_t nal_type = pos[i + 4] & 0x1F;

      if (nal_type == 7) {
        int width = 0;
        int height = 0;
        int fps = 0;
        
        h264_decode_sps(pos + 4, buffer_size - 4, &width, &height, &fps);

        if (width == 1072) {
          save_stream = 1;
        }
      }

      if (nal_type != 7 && nal_type != 8) {
        video_frames++;
      }
      i += 4;
    } 
  }
}

static wx_error_t video_stream_write(
    struct wx_video_stream* stream,
    const void* buffer,
    size_t buffer_size,
    const struct wx_video_stream_data_metadata* metadata) {
  
  static int64_t last_video_stream_ts = 0;
  if (last_video_stream_ts != 0) {

    /* 统计桢间隔大于 500ms 的*/
    if ((get_timestamp_us() - last_video_stream_ts)/1000 > 500) {
      printf("%s !!!!!!!!!!!!! delta:%ldms\n", __FUNCTION__, (get_timestamp_us() - last_video_stream_ts)/1000);
    }
  }

  last_video_stream_ts = get_timestamp_us();

  if (voip_receive_video == 0) {
    extern int64_t voip_status_2_ts;

    printf("%s !!!!!!!!!!!!! video receive delay: %ldms\n", __FUNCTION__, (get_timestamp_us() - voip_status_2_ts)/1000);
  }

  voip_receive_video = 1;
  
  if (video_fd == 0) {
    video_fd = open("video.h264", O_RDWR | O_CREAT, 0700);
  }

  if (video_fd <= 0) {
    printf("open video.h264 fail\n");
    return WXERROR_RESOURCE_EXHAUSTED;
  }

  write(video_fd, buffer, buffer_size);

  h264_sps_paser((uint8_t *)buffer, buffer_size);

  video_total_len += buffer_size;

  if (receive_video_stream_ts == 0) {
    receive_video_stream_ts = get_timestamp_us();
  }

  if ((get_timestamp_us() - receive_video_stream_ts) >= 5000*1000) {
    printf("video fps: %d  video rate: %ld kbps\n", video_frames/5, video_total_len/5*8/1024);
    video_frames = 0;
    video_total_len = 0;
    
    receive_video_stream_ts = get_timestamp_us();
  }


  return WXERROR_OK;
}

static void video_stream_close(struct wx_video_stream* stream) {
  printf("%s \n", __FUNCTION__);
}

static struct wx_video_stream* alloc_video_stream(void) {
  struct wx_video_stream* stream =
      (struct wx_video_stream*)calloc(1, sizeof(struct wx_video_stream));

  if (stream) {
    stream->common.size = sizeof(struct wx_video_stream);
    stream->common.tag = WX_VIDEO_STREAM_TAG;
    stream->common.version = 0;

    stream->write = video_stream_write;
    stream->close = video_stream_close;
  }

  return stream;
}

static wx_error_t video_create_output_stream(
    struct wx_video_module* module,
    const struct wx_video_stream_config* config,
    struct wx_video_stream** stream_out) {
  VIDEO_STREAM* stream = (VIDEO_STREAM*)calloc(1, sizeof(VIDEO_STREAM));

  printf("%s \n", __FUNCTION__);

  if (stream) {
    struct wx_video_stream* _stream = alloc_video_stream();

    if (_stream) {
      printf("video format = %x\n", config->format);

      memcpy(&stream->config, config, sizeof(struct wx_video_stream_config));
      stream->stream = _stream;

      list_add_tail(&video_stream_list, &stream->slist);
      *stream_out = _stream;

      return WXERROR_OK;
    }

    free(stream);
  }

  return WXERROR_RESOURCE_EXHAUSTED;
}

struct wx_video_module video_module = {
    .common =
        {
            .common =
                {
                    .tag = WX_VIDEO_MODULE_TAG,
                    .size = sizeof(wx_video_module_t),
                    .version = 0,
                },
            .id = WX_VIDEO_MODULE_ID,
            .set_on_devices_changed = NULL,
        },

    .create_encoder = NULL,
    .create_decoder = NULL,
    .create_output_stream = video_create_output_stream,
};