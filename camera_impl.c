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
#include "wmpf/hardware/camera.h"
#include "wmpf/wmpf.h"

extern int h265_camera;

#include "list.h"

typedef struct {
  struct wx_camera_stream_config config;
  const struct wx_camera_stream_listener* listener;
  void* user_data;
  struct wx_camera_stream* stream;
  struct listnode slist;
} CAMERA_STREAM;

static list_declare(camera_stream_list);
extern wx_cloudvoip_session_status_t voip_status;

static pthread_mutex_t camera_mutex;

static void dump_cameracfg(const struct wx_camera_stream_config* config) {
  printf("\tconfig->format = %x\n", config->format);
  printf("\tconfig->pixel_format = %x\n", config->pixel_format);
}

static void camera_device_close(struct wx_device* device) {}

static wx_error_t camera_device_getparameter(
    const struct wx_metadata* metadata,
    uint32_t key,
    struct wx_metadata_entry* value_out) {
  uint32_t size[2] = {640, 480};

  if (!value_out) {
    return WXERROR_INVALID_ARGUMENT;
  }

  switch (key) {
    case WX_CAMERA_DEVICE_METADATA_FACING:
      wx_metadata_entry_set_u32(value_out, key, WX_CAMERA_FACING_FRONT);
      return WXERROR_OK;

    case WX_CAMERA_DEVICE_METADATA_FRAMERATE:
      wx_metadata_entry_set_u32(value_out, key, 30);
      return WXERROR_OK;

    case WX_CAMERA_DEVICE_METADATA_BITRATE:
      wx_metadata_entry_set_u32(value_out, key, 200000);
      return WXERROR_OK;

    case WX_CAMERA_DEVICE_METADATA_SIZE:
      wx_metadata_entry_set_u32_array(value_out, key, size, 2);
      return WXERROR_OK;

    default:
      return WXERROR_NOT_FOUND;
  }
}

static wx_error_t camera_device_setparameter(
    struct wx_metadata* metadata,
    uint32_t key,
    const struct wx_metadata_entry* value) {
  if (!value) {
    return WXERROR_INVALID_ARGUMENT;
  }

  switch (value->key) {
    case WX_CAMERA_DEVICE_METADATA_FRAMERATE:
      if (value->type != WX_METADATA_ENTRY_TYPE_U32)
        return WXERROR_INVALID_ARGUMENT;

      printf("camear fps = %d\n", value->data.u32);
      return WXERROR_OK;

    case WX_CAMERA_DEVICE_METADATA_SIZE:
      if (value->type != WX_METADATA_ENTRY_TYPE_U32_ARRAY || value->count != 2)
        return WXERROR_INVALID_ARGUMENT;

      printf("camera size %d %d\n", value->data.u32_array[0],
             value->data.u32_array[1]);
      return WXERROR_OK;

    default:
      return WXERROR_NOT_FOUND;
  }
}

static bool camera_supports(struct wx_camera_device* dev,
                            const struct wx_camera_stream_config* config) {
  return true;
}

static void camera_stream_close(struct wx_camera_stream* stream) {
  printf("camera_stream_close\n");
  dump_cameracfg(&stream->config);

  pthread_mutex_lock(&camera_mutex);

  struct listnode* node;
  list_for_each(node, &camera_stream_list) {
    CAMERA_STREAM* _stream = node_to_item(node, CAMERA_STREAM, slist);

    if (_stream->stream == stream) {
      list_remove(&_stream->slist);
      free(_stream->stream);
      free(_stream);
      break;
    }
  }

  pthread_mutex_unlock(&camera_mutex);

  return;
}

static struct wx_camera_stream* alloc_camera_stream(void) {
  struct wx_camera_stream* stream =
      (struct wx_camera_stream*)calloc(1, sizeof(struct wx_camera_stream));

  if (stream) {
    stream->common.size = sizeof(struct wx_camera_stream);
    stream->common.tag = WX_CAMERA_STREAM_TAG;
    stream->common.version = 0;

    stream->config.format = WX_VIDEO_FORMAT_H264;
    stream->config.pixel_format = WX_VIDEO_PIXEL_FORMAT_YUV420;

    stream->close = camera_stream_close;
  }

  return stream;
}

static wx_error_t camera_open_stream(
    struct wx_camera_device* dev,
    const struct wx_camera_stream_config* config,
    const struct wx_camera_stream_listener* listener,
    void* user_data,
    struct wx_camera_stream** stream_out) {
  printf("%s user_data=%p\n", __FUNCTION__, user_data);
  dump_cameracfg(config);

  if (h265_camera) {
    if (config->format == WX_VIDEO_FORMAT_H265) {
      printf("camera_open_stream use h265\n");
    }
    if (config->format == WX_VIDEO_FORMAT_H264) {
      printf("camera_open_stream not use h264\n");
      return WXERROR_UNIMPLEMENTED;
    }
  }
  else {
    if (config->format == WX_VIDEO_FORMAT_H265) {
      printf("camera_open_stream not use h265\n");
      return WXERROR_UNIMPLEMENTED;
    }
    if (config->format == WX_VIDEO_FORMAT_H264) {
      printf("camera_open_stream use h264\n");
    }
  }

  CAMERA_STREAM* stream = (CAMERA_STREAM*)calloc(1, sizeof(CAMERA_STREAM));

  if (stream) {
    struct wx_camera_stream* _stream = alloc_camera_stream();

    if (_stream) {
      memcpy(&stream->config, config, sizeof(struct wx_camera_stream_config));
      stream->listener = listener;
      stream->stream = _stream;
      stream->user_data = user_data;

      list_add_tail(&camera_stream_list, &stream->slist);
      *stream_out = _stream;

      return WXERROR_OK;
    }

    free(stream);
  }

  return WXERROR_RESOURCE_EXHAUSTED;
}

static struct wx_camera_device camera_device = {
    .common =
        {
            .common =
                {
                    .tag = WX_CAMERA_DEVICE_TAG,
                    .size = sizeof(struct wx_camera_device),
                    .version = 0,
                },
            .priv = NULL,
            .close = camera_device_close,
        },
    .metadata =
        {
            .get_parameter = camera_device_getparameter,
            .set_parameter = camera_device_setparameter,
            .priv = &camera_device,
        },
    .supports = camera_supports,
    .open_stream = camera_open_stream,
};

static wx_error_t camera_get_number_of_devices(struct wx_camera_module* module,
                                               size_t* num_devices_out) {
  *num_devices_out = 1;
  return WXERROR_OK;
}

static wx_error_t camera_get_device_info(
    struct wx_camera_module* module,
    size_t index,
    struct wx_camera_device_info* device_info) {
  printf("%s index = %zu\n", __FUNCTION__, index);
  return WXERROR_OK;
}

static wx_error_t camera_open(struct wx_camera_module* module,
                              const char* id,
                              struct wx_camera_device** device_out) {
  printf("%s %s\n", __FUNCTION__, id);

  if (!device_out) {
    return WXERROR_INVALID_ARGUMENT;
  }

  if (!strcmp(id, WX_CAMERA_DEVICE_PRIMARY)) {
    *device_out = (struct wx_camera_device*)&camera_device;
    return WXERROR_OK;
  }

  return WXERROR_NOT_FOUND;
}

struct wx_camera_module camera_module = {
    .common =
        {
            .common =
                {
                    .tag = WX_CAMERA_MODULE_TAG,
                    .size = sizeof(struct wx_camera_module),
                    .version = 0,
                },
            .id = WX_CAMERA_MODULE_ID,
            .set_on_devices_changed = NULL,
        },
    .get_number_of_devices = camera_get_number_of_devices,
    .get_device_info = camera_get_device_info,
    .open = camera_open,
};

static pthread_t pid = 0;
static int thread_exit = 0;

extern int h264_init(const char* path);
int h264_readnalu(uint8_t* p, size_t* size, int* type);

static uint8_t buffer[10 * 1024 * 1024];

static void* thread_camera(void* data) {
  int video_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (video_fd < 0) {
    printf("Create video UDP socket failed\n");
    return NULL;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9002);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int reuse = 1;
  setsockopt(video_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 50000; // 50ms timeout
  setsockopt(video_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (bind(video_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("Bind video UDP socket to 9002 failed\n");
    close(video_fd);
    return NULL;
  }

  printf("[*] Video UDP receiver thread started on 127.0.0.1:9002\n");

  while (!thread_exit) {
    if (voip_status == WX_CLOUDVOIP_SESSION_TALKING) {
      ssize_t sz = recvfrom(video_fd, buffer, sizeof(buffer), 0, NULL, NULL);
      if (sz > 0) {
        pthread_mutex_lock(&camera_mutex);

        struct listnode* node;
        list_for_each(node, &camera_stream_list) {
          CAMERA_STREAM* stream = node_to_item(node, CAMERA_STREAM, slist);

          if (stream->config.format == WX_VIDEO_FORMAT_H264) {
            struct timespec ts = {0};
            stream->listener->data(stream->stream, stream->user_data, buffer, sz,
                                  0, 0, WX_VIDEO_ROTATION_0, ts);
          }
        }

        pthread_mutex_unlock(&camera_mutex);
      }
    }
    else {
      usleep(20000);
    }
  }

  close(video_fd);
  printf("thread_camera exit\n");
  return NULL;
}

void stop_camera_thread(void) {
  thread_exit = 1;
  pthread_join(pid, NULL);
}

void start_camera_thread(void) {
  pthread_create(&pid, NULL, thread_camera, NULL);
}
