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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "wmpf/cloudvoip_server.h"
#include "wmpf/hardware/audio.h"
#include "wmpf/wmpf.h"

#include "list.h"

#undef AUDIO_OPUS_DEMO

static pthread_mutex_t audio_player_mutex;
static pthread_mutex_t audio_record_mutex;

typedef struct {
  struct wx_audio_config config;
  const struct wx_audio_stream_out_listener* listener;
  void* user_data;
  struct wx_audio_stream_out* stream_out;
  struct listnode slist;
} AUDIO_STREAMOUT;

typedef struct {
  struct wx_audio_config config;
  const struct wx_audio_stream_in_listener* listener;
  void* user_data;
  struct wx_audio_stream_in* stream_in;
  struct listnode slist;
} AUDIO_STREAMIN;

static list_declare(audio_out_stream_list);
static list_declare(audio_in_stream_list);

extern wx_cloudvoip_session_status_t voip_status;
extern int64_t get_timestamp_us();

#define countof(x) (sizeof(x) / sizeof((x)[0]))

static const uint32_t kSupportedFormats[] = {
#ifdef AUDIO_OPUS_DEMO
  WX_AUDIO_FORMAT_OPUS,
#else
  WX_AUDIO_FORMAT_PCM,
#endif
};

static void audio_device_close(struct wx_device* device) {
  printf("%s\n", __FUNCTION__);
}

static wx_error_t audio_device_getparameter(
    const struct wx_metadata* metadata,
    uint32_t key,
    struct wx_metadata_entry* value_out) {
  switch (key) {
    case WX_AUDIO_DEVICE_METADATA_MUTE:
      return WXERROR_NOT_FOUND;
    case WX_AUDIO_DEVICE_METADATA_VOLUME:
      return WXERROR_NOT_FOUND;
    case WX_AUDIO_DEVICE_METADATA_SUPPORTED_FORMATS:
      value_out->count = countof(kSupportedFormats);
      value_out->key = key;
      value_out->type = WX_METADATA_ENTRY_TYPE_U32_ARRAY,
      value_out->data.u32_array = kSupportedFormats;
      return WXERROR_OK;
    default:
      return WXERROR_NOT_FOUND;
  }
}

static wx_error_t audio_device_setparameter(
    struct wx_metadata* metadata,
    uint32_t key,
    const struct wx_metadata_entry* value) {
  switch (key) {
    case WX_AUDIO_DEVICE_METADATA_MUTE:
      return WXERROR_NOT_FOUND;
    case WX_AUDIO_DEVICE_METADATA_VOLUME:
      return WXERROR_NOT_FOUND;
    default:
      return WXERROR_NOT_FOUND;
  }
}

static wx_error_t audio_get_number_of_devices(
    struct wx_audio_module* module,
    wx_audio_device_type_t device_type,
    size_t* num_devices_out) {
  printf("%s device_type = %d\n", __FUNCTION__, device_type);

  *num_devices_out = 1;

  return WXERROR_OK;
}

static wx_error_t audio_get_device_name(struct wx_audio_module* module,
                                        size_t index,
                                        wx_audio_device_type_t device_type,
                                        char** device_name_out) {
  printf("%s index = %zu device_type =%d\n", __FUNCTION__, index, device_type);

  if (device_type == WX_AUDIO_DEVICE_IN) {
    *device_name_out = strdup("fake audio in");
  }

  if (device_type == WX_AUDIO_DEVICE_OUT) {
    *device_name_out = strdup("fake audio out");
  }

  return WXERROR_OK;
}

static void audio_stream_in_close(struct wx_audio_stream* stream) {
  struct listnode* node;

  /*
   * audio_in_stream_list 中的 stream 流需要关闭
   */

  pthread_mutex_lock(&audio_record_mutex);

  list_for_each(node, &audio_in_stream_list) {
    AUDIO_STREAMIN* _stream = node_to_item(node, AUDIO_STREAMIN, slist);

    if (_stream->stream_in == (struct wx_audio_stream_in*)stream) {
      printf("free in stream %p\n", _stream);

      list_remove(&_stream->slist);
      free(_stream->stream_in);
      free(_stream);
      break;
    }
  }

  pthread_mutex_unlock(&audio_record_mutex);
}

static void audio_stream_out_close(struct wx_audio_stream* stream) {
  struct listnode* node;

  /*
   * audio_out_stream_list 中的 stream 流需要关闭
   */

  pthread_mutex_lock(&audio_player_mutex);

  list_for_each(node, &audio_out_stream_list) {
    AUDIO_STREAMOUT* _stream = node_to_item(node, AUDIO_STREAMOUT, slist);

    if (_stream->stream_out == (struct wx_audio_stream_out*)stream) {
      printf("free out stream %p\n", _stream);

      list_remove(&_stream->slist);
      free(_stream->stream_out);
      free(_stream);
      break;
    }
  }

  pthread_mutex_unlock(&audio_player_mutex);
}

static wx_error_t audio_stream_in_pause(struct wx_audio_stream_in* dev) {
  printf("%s\n", __FUNCTION__);
  return WXERROR_OK;
}

static wx_error_t audio_stream_in_resume(struct wx_audio_stream_in* dev) {
  printf("%s\n", __FUNCTION__);
  return WXERROR_OK;
}

static wx_error_t audio_stream_out_flush(struct wx_audio_stream_out* stream) {
  printf("%s\n", __FUNCTION__);
  return WXERROR_OK;
}

static wx_error_t audio_stream_out_pause(struct wx_audio_stream_out* dev) {
  printf("%s\n", __FUNCTION__);
  return WXERROR_OK;
}

static wx_error_t audio_stream_out_resume(struct wx_audio_stream_out* dev) {
  printf("%s\n", __FUNCTION__);
  return WXERROR_OK;
}

static struct wx_audio_stream_in* alloc_stream_in(void) {
  struct wx_audio_stream_in* stream =
      (struct wx_audio_stream_in*)calloc(1, sizeof(struct wx_audio_stream_in));

  if (stream) {
    stream->common.common.size = sizeof(struct wx_audio_stream_in);
    stream->common.common.tag = WX_AUDIO_STREAM_IN_TAG;
    stream->common.common.version = 0;
    stream->common.close = audio_stream_in_close;

    stream->pause = audio_stream_in_pause;
    stream->resume = audio_stream_in_resume;
  }

  return stream;
}

static struct wx_audio_stream_out* alloc_stream_out(void) {
  struct wx_audio_stream_out* stream = (struct wx_audio_stream_out*)calloc(
      1, sizeof(struct wx_audio_stream_out));

  if (stream) {
    stream->common.common.size = sizeof(struct wx_audio_stream_out);
    stream->common.common.tag = WX_AUDIO_STREAM_OUT_TAG;
    stream->common.common.version = 0;
    stream->common.close = audio_stream_out_close;

    stream->pause = audio_stream_out_pause;
    stream->resume = audio_stream_out_resume;
    stream->flush = audio_stream_out_flush;
  }

  return stream;
}

static void dump_audiocfg(const struct wx_audio_config* config) {
  printf("\tsample_rate %d\n", config->sample_rate);
  printf("\tchannel_layout %x\n", config->channel_layout);
  printf("\tsample_format %x\n", config->sample_format);
  printf("\tframe_count %zu\n", config->frame_count);
  printf("\tbytes = %zu\n", wx_audio_config_get_bytes_per_frame(config));
}

static wx_error_t audio_open_input_stream(
    struct wx_audio_device_in* dev,
    const struct wx_audio_config* config,
    const struct wx_audio_stream_in_listener* listener,
    void* user_data,
    struct wx_audio_stream_in** stream_out) {
  printf("%s user_data=%p\n", __FUNCTION__, user_data);
  dump_audiocfg(config);

  AUDIO_STREAMIN* stream = (AUDIO_STREAMIN*)calloc(1, sizeof(AUDIO_STREAMIN));

  if (stream) {
    struct wx_audio_stream_in* _stream_in = alloc_stream_in();

    if (_stream_in) {
      memcpy(&stream->config, config, sizeof(struct wx_audio_config));
      stream->listener = listener;
      stream->stream_in = _stream_in;
      stream->user_data = user_data;

      list_add_tail(&audio_in_stream_list, &stream->slist);
      *stream_out = _stream_in;

      printf("audio_open_input_stream stream = %p return %p\n", stream,
             _stream_in);
      return WXERROR_OK;
    }

    free(stream);
  }

  return WXERROR_RESOURCE_EXHAUSTED;
}

static wx_error_t audio_open_output_stream(
    struct wx_audio_device_out* dev,
    const struct wx_audio_config* config,
    const struct wx_audio_stream_out_listener* listener,
    void* user_data,
    struct wx_audio_stream_out** stream_out) {
  printf("%s user_data=%p\n", __FUNCTION__, user_data);
  dump_audiocfg(config);

  AUDIO_STREAMOUT* stream =
      (AUDIO_STREAMOUT*)calloc(1, sizeof(AUDIO_STREAMOUT));

  if (stream) {
    struct wx_audio_stream_out* _stream_out = alloc_stream_out();

    if (_stream_out) {
      memcpy(&stream->config, config, sizeof(struct wx_audio_config));
      stream->listener = listener;
      stream->stream_out = _stream_out;
      stream->user_data = user_data;

      list_add_tail(&audio_out_stream_list, &stream->slist);
      *stream_out = _stream_out;

      printf("audio_open_output_stream stream = %p return %p\n", stream,
             _stream_out);
      return WXERROR_OK;
    }

    free(stream);
  }

  return WXERROR_RESOURCE_EXHAUSTED;
}

static struct wx_audio_device_in audio_in_device = {
    .device =
        {
            .common =
                {
                    .common =
                        {
                            .tag = WX_AUDIO_DEVICE_IN_TAG,
                            .size = sizeof(struct wx_audio_device_in),
                            .version = 0,
                        },
                    .priv = NULL,
                    .close = audio_device_close,
                },
            .metadata =
                {
                    .get_parameter = audio_device_getparameter,
                    .set_parameter = audio_device_setparameter,
                    .priv = &audio_in_device,
                },
        },

    .open_input_stream = audio_open_input_stream,
};

static struct wx_audio_device_out audio_out_device = {
    .device =
        {
            .common =
                {
                    .common =
                        {
                            .tag = WX_AUDIO_DEVICE_OUT_TAG,
                            .size = sizeof(struct wx_audio_device_out),
                            .version = 0,
                        },
                    .priv = NULL,
                    .close = audio_device_close,
                },
            .metadata =
                {
                    .get_parameter = audio_device_getparameter,
                    .set_parameter = audio_device_setparameter,
                    .priv = &audio_out_device,
                },
        },

    .open_output_stream = audio_open_output_stream,
};

static wx_error_t audio_open(struct wx_audio_module* module,
                             const char* id,
                             wx_audio_device_type_t device_type,
                             struct wx_audio_device** device_out) {
  printf("%s %s device_type %d\n", __FUNCTION__, id, device_type);

  if (!strcmp(id, WX_AUDIO_DEVICE_PRIMARY)) {
    if (device_type == WX_AUDIO_DEVICE_IN) {
      *device_out = (struct wx_audio_device*)&audio_in_device;
    }

    if (device_type == WX_AUDIO_DEVICE_OUT) {
      *device_out = (struct wx_audio_device*)&audio_out_device;
    }
  }

  return WXERROR_OK;
}

static struct wx_metadata audio_info_metadata = {
  .get_parameter = audio_device_getparameter,
  .set_parameter = audio_device_setparameter,
  .priv = &audio_out_device,
};

wx_error_t audio_get_device_info(struct wx_audio_module* module,
                                const char* id,
                                wx_audio_device_type_t device_type,
                                const struct wx_metadata** metadata_out)
{
  *metadata_out = &audio_info_metadata;
  return WXERROR_OK;
}

struct wx_audio_module audio_module = {
    .common =
        {
            .common =
                {
                    .tag = WX_AUDIO_MODULE_TAG,
                    .size = sizeof(wx_audio_module_t),
                    .version = 0,
                },
            .id = WX_AUDIO_MODULE_ID,
            .set_on_devices_changed = NULL,
        },

    .get_number_of_devices = audio_get_number_of_devices,
    .get_device_name = audio_get_device_name,
    .get_device_info = audio_get_device_info,
    .open = audio_open,
};

static int thread_exit = 0;
static pthread_t pid1 = 0, pid2 = 0;

extern int voip_receive_video;

static int play_udp_fd = -1;
static struct sockaddr_in play_udp_addr;

static int64_t receive_pcm_stream_ts = 0;
static int video_delay_ms = 0;

static uint8_t buffer_zero[128] = {0};

static void hal_play_buffer(uint8_t* buffer, size_t len) {

  //printf("hal_play_buffer len %zu\n", len);

  //if (memcmp(buffer, buffer_zero, 128) == 0) {
  //  printf("mute..\n");
  //}
  /*
   * todo: 在此实现 buffer 的实际播放
   */
  if (len == 0) {
    printf("hal_play_buffer null~\n");
    return;
  }

  if (receive_pcm_stream_ts == 0) {
    receive_pcm_stream_ts = get_timestamp_us();
  }

  /*
   * (Removed hardcoded voip_receive_video check to allow audio-only calls)
   */

  /* 测试视频滞后音频的时间 */
  if (video_delay_ms == 0) {
    video_delay_ms = (get_timestamp_us() - receive_pcm_stream_ts)/1000;
    printf("receive audio/video, delay = %d ms\n", video_delay_ms);
  }

  static int fd = 0;
  
  if (fd == 0) {
    #ifdef AUDIO_OPUS_DEMO
    fd = open("play.opus", O_RDWR | O_CREAT, 0700);
    #else
    fd =open("play.pcm", O_RDWR | O_CREAT, 0700);
    #endif
  }

  if (fd < 0) {
    printf("open play.opus fail\n");
    return;
  }

#ifdef AUDIO_OPUS_DEMO
  uint32_t magic = 0x20200820;
  uint32_t data_len = (uint32_t)len;

  write(fd, &magic, 4);
  write(fd, &data_len, 4);
  write(fd, buffer, data_len);
#else
  write(fd, buffer, len);
#endif

  // Send PCM audio data to the python TCP relay via UDP port 9004
  if (play_udp_fd >= 0) {
    sendto(play_udp_fd, buffer, len, 0, (struct sockaddr*)&play_udp_addr, sizeof(play_udp_addr));
  }
}

static void* thread_audio_player(void* data) {

  /*
   * 每隔 PLAYAUDIO_DELAYMS 播放一次数据，开发者可以调整此时间 
   * */
  #define PLAYAUDIO_PCM_DELAYMS 20
  #define PLAYAUDIO_OPUS_DELAYMS 20

  /* 每次播放数据的大小，这里根据 16000 采样 S16_LE 类型来计算 */
  #define THUNK_PLAYER_DATA_SIZE 16000 / 1000 * PLAYAUDIO_PCM_DELAYMS * 2

  while (!thread_exit) {
    if (voip_status == WX_CLOUDVOIP_SESSION_TALKING) {
      uint8_t buffer[THUNK_PLAYER_DATA_SIZE] = {0};
      struct timespec ts = {0};

      pthread_mutex_lock(&audio_player_mutex);

      /*
       * 将链表里每一路 stream, 通过其 listener 的 data 接口来获取数据到 buffer
       */
      struct listnode* node;
      list_for_each(node, &audio_out_stream_list) {
        AUDIO_STREAMOUT* stream;
        size_t data_len = 0;

        stream = node_to_item(node, AUDIO_STREAMOUT, slist);

        if (stream->config.sample_rate == WX_AUDIO_STREAM_SAMPLE_RATE_16000 &&
            stream->config.sample_format == WX_SAMPLE_FORMAT_S16) {
          /*
           * 如果是 16000 S16 数据，直接取到 buffer 里
           */
          data_len =
              stream->listener->data(stream->stream_out, stream->user_data, ts,
                                     ts, buffer, THUNK_PLAYER_DATA_SIZE);
          
          // 这里主要关心 16000 的数据，它是 voip 语音通话的 sample_rate
          hal_play_buffer(buffer, data_len);

        } else if (stream->config.sample_rate ==
                   WX_AUDIO_STREAM_SAMPLE_RATE_44100) {
          // ...
          // 其它格式的也需要取
          //

					data_len =
              stream->listener->data(stream->stream_out, stream->user_data, ts,
                                     ts, buffer, THUNK_PLAYER_DATA_SIZE);
					printf("WX_AUDIO_STREAM_SAMPLE_RATE_44100\n");
        } else if (stream->config.sample_rate ==
                   WX_AUDIO_STREAM_SAMPLE_RATE_8000) {
          // ...
          // 其它格式的也需要取
          //

					data_len =
              stream->listener->data(stream->stream_out, stream->user_data, ts,
                                     ts, buffer, THUNK_PLAYER_DATA_SIZE);
					printf("WX_AUDIO_STREAM_SAMPLE_RATE_8000\n");
        }
      }

      pthread_mutex_unlock(&audio_player_mutex);
    }

#ifdef AUDIO_OPUS_DEMO
    usleep(1000 * PLAYAUDIO_OPUS_DELAYMS);
#else
    usleep(1000 * PLAYAUDIO_PCM_DELAYMS);
#endif
  }

  printf("exit %s\n", __FUNCTION__);
  return NULL;
}

/*
 * 16000Hz mono S16 => 320 samples = 640 bytes per 20ms frame, which is exactly
 * what the WeChat SDK expects per listener->data() call.
 */
#define REC_FRAME_BYTES     (16000 / 1000 * 20 * 2)   /* 640  -> 20ms          */
#define REC_PREBUFFER_BYTES (REC_FRAME_BYTES * 3)      /* ~60ms before playout  */
#define REC_MAX_BYTES       (REC_FRAME_BYTES * 25)     /* ~500ms latency cap    */

static void feed_record_frame(const uint8_t* frame, size_t len) {
  struct timespec ts = {0};
  struct listnode* node;

  pthread_mutex_lock(&audio_record_mutex);
  list_for_each(node, &audio_in_stream_list) {
    AUDIO_STREAMIN* stream = node_to_item(node, AUDIO_STREAMIN, slist);
    if (stream->config.sample_rate == WX_AUDIO_STREAM_SAMPLE_RATE_16000 &&
        stream->config.sample_format == WX_SAMPLE_FORMAT_S16) {
      stream->listener->data(stream->stream_in, stream->user_data, ts,
                             (uint8_t*)frame, len);
    }
  }
  pthread_mutex_unlock(&audio_record_mutex);
}

static void* thread_audio_record(void* data) {
  int audio_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (audio_fd < 0) {
    printf("Create audio UDP socket failed\n");
    return NULL;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9003);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int reuse = 1;
  setsockopt(audio_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  /*
   * Short receive timeout so the loop wakes often enough to keep a steady 20ms
   * playout clock instead of feeding the encoder as fast as packets arrive.
   */
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 5000; // 5ms
  setsockopt(audio_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (bind(audio_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("Bind audio UDP socket to 9003 failed\n");
    close(audio_fd);
    return NULL;
  }

  printf("[*] Audio UDP receiver thread started on 127.0.0.1:9003 (jitter-buffered)\n");

  uint8_t udp_buf[4096];

  /*
   * Jitter buffer: the device pushes real-time audio over a lossy Wi-Fi/4G TCP
   * link, so packets reach us in bursts. Feeding those bursts straight into the
   * WeChat encoder makes the far end (mini-program) hear crackle / static. We
   * accumulate incoming PCM here and hand the encoder exactly one 640-byte
   * frame every 20ms.
   */
  static uint8_t jbuf[REC_MAX_BYTES + 4096];
  size_t   jlen = 0;         // valid bytes in jbuf
  int      feeding = 0;      // started playout after prebuffer filled?
  int64_t  next_feed_us = 0; // wall-clock time of the next frame to feed

  while (!thread_exit) {
    if (voip_status != WX_CLOUDVOIP_SESSION_TALKING) {
      // Drop buffered audio between calls so a new call starts clean.
      jlen = 0;
      feeding = 0;
      usleep(10000);
      continue;
    }

    /* 1) Pull whatever is available into the jitter buffer. */
    ssize_t sz = recvfrom(audio_fd, udp_buf, sizeof(udp_buf), 0, NULL, NULL);
    if (sz > 0) {
      if (jlen + (size_t)sz > REC_MAX_BYTES) {
        /* Bound latency: drop oldest whole frames to keep S16 alignment. */
        size_t overflow = jlen + (size_t)sz - REC_MAX_BYTES;
        overflow = ((overflow + REC_FRAME_BYTES - 1) / REC_FRAME_BYTES) * REC_FRAME_BYTES;
        if (overflow > jlen) {
          overflow = jlen;
        }
        memmove(jbuf, jbuf + overflow, jlen - overflow);
        jlen -= overflow;
        printf("[!] audio jitter buffer overflow, dropped %zu bytes\n", overflow);
      }
      memcpy(jbuf + jlen, udp_buf, (size_t)sz);
      jlen += (size_t)sz;
    }

    /* 2) Wait for a small prebuffer before starting steady playout. */
    int64_t now = get_timestamp_us();
    if (!feeding && jlen >= REC_PREBUFFER_BYTES) {
      feeding = 1;
      next_feed_us = now;
    }

    /* 3) Feed one frame per 20ms tick (catching up if we fell behind). */
    while (feeding && now >= next_feed_us) {
      if (jlen < REC_FRAME_BYTES) {
        /* Underrun: pause and rebuild the prebuffer to avoid choppy audio. */
        feeding = 0;
        break;
      }
      feed_record_frame(jbuf, REC_FRAME_BYTES);
      memmove(jbuf, jbuf + REC_FRAME_BYTES, jlen - REC_FRAME_BYTES);
      jlen -= REC_FRAME_BYTES;
      next_feed_us += 20000;
    }
  }

  close(audio_fd);
  printf("exit thread_audio_record\n");
  return NULL;
}


static void* thread_audio_record_opus(void* data) {

  #define THUNK_OPUS_MAX_FRAMESIZE 4096

  // input.opus 文件中的每一桢为 20ms 的数据
  #define RECORD_OPUS_DELAYMS 20

  uint8_t buffer[THUNK_OPUS_MAX_FRAMESIZE] = {0};

  int fd = open("input.opus", O_RDONLY, 0700);
  if (fd < 0) {
    printf("open input.opus fail\n");
    return NULL;
  }

  while (!thread_exit) {
    if (voip_status == WX_CLOUDVOIP_SESSION_TALKING) {
      struct timespec ts = {0};

      uint32_t magic = 0;
      uint32_t data_size = 0;

      /*
       * input.opus 文件组成：
       *   magic(4) + data_dize(4) + data(data_size)
       */

      if (read(fd, &magic, 4) != 4) {
        printf("read opus magic head fail\n");
        lseek(fd, 0, SEEK_SET);
        continue;
      }

      if (read(fd, &data_size, 4) != 4) {
        printf("read opus data size fail\n");
        lseek(fd, 0, SEEK_SET);
        continue;
      }

      if (read(fd, buffer, data_size) != data_size) {
        printf("read opus data fail\n");
        lseek(fd, 0, SEEK_SET);
        continue;
      }

      pthread_mutex_lock(&audio_record_mutex);

      /*
       * 将链表里每一路 stream_in, 使用pcm文件里的数据，通过其 listener 的 data
       * 接口来发送
       */
      struct listnode* node;
      list_for_each(node, &audio_in_stream_list) {
        AUDIO_STREAMIN* stream;

        stream = node_to_item(node, AUDIO_STREAMIN, slist);

        if (stream->config.sample_rate == WX_AUDIO_STREAM_SAMPLE_RATE_16000 &&
            stream->config.sample_format == WX_SAMPLE_FORMAT_S16) {
          /*
           * PCM 文件里是 16000 S16 数据，如果 stream 是这种格式，直接发送
           */
          
          stream->listener->data(stream->stream_in, stream->user_data, ts,
                                 buffer, data_size);
        }
        if (stream->config.sample_rate == WX_AUDIO_STREAM_SAMPLE_RATE_8000) {
          // ...
          // PCM 文件是 16000 S16 数据，这里要 resample 再发送
          //
        }
      }

      pthread_mutex_unlock(&audio_record_mutex);
    }

    usleep(1000 * RECORD_OPUS_DELAYMS);
  }

  close(fd);

  printf("exit %s\n", __FUNCTION__);
  return NULL;
}

void start_audio_thread(void) {
  /*
   * 创建 play 线程用来播放设备收到微信端的数据
   * 创建 record
   * 线程用来发送数据给微信端，若开发者使用真实硬件，可能不需要创建线程，直接在录音数据的地方发送即可
   *
   */
  play_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (play_udp_fd >= 0) {
    memset(&play_udp_addr, 0, sizeof(play_udp_addr));
    play_udp_addr.sin_family = AF_INET;
    play_udp_addr.sin_port = htons(9004);
    play_udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    printf("[*] Created play UDP socket to send downstream audio to 127.0.0.1:9004\n");
  } else {
    printf("[-] Failed to create play UDP socket\n");
  }

  pthread_mutex_init(&audio_player_mutex, NULL);
  pthread_mutex_init(&audio_record_mutex, NULL);

  pthread_create(&pid1, NULL, thread_audio_player, NULL);
#ifdef AUDIO_OPUS_DEMO
  pthread_create(&pid2, NULL, thread_audio_record_opus, NULL);
#else
  pthread_create(&pid2, NULL, thread_audio_record, NULL);
#endif
}

void stop_audio_thread(void) {
  thread_exit = 1;
  pthread_join(pid1, NULL);
  pthread_join(pid2, NULL);

  if (play_udp_fd >= 0) {
    close(play_udp_fd);
    play_udp_fd = -1;
  }

  pthread_mutex_destroy(&audio_player_mutex);
  pthread_mutex_destroy(&audio_record_mutex);
}
