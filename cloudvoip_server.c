// Copyright (c) 2023, Tencent Inc.
// All rights reserved.
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "wmpf/cloudvoip_server.h"
#include "wmpf/types.h"
#include "wmpf/wmpf.h"

#include "wmpf/crypto.h"
#include "wmpf/hardware/audio.h"
#include "wmpf/hardware/camera.h"
#include "wmpf/hardware/video.h"

void start_audio_thread(void);
void start_camera_thread(void);
void stop_audio_thread();
void stop_camera_thread();

static char device_id[256] = {0};
static char model_id[256] = {0};
static char appid[256] = {0};
static char payload[256] = {0};
static char token[256] = {0};
static char roomid[256] = {0};
static char session_key[256] = {0};

static int subscribe_video_length = 0;
static int subscribe_video_rotation = 0;
static int subscribe_video_ratio = 0;
static int subscribe_video_maxfps = 0;
static int listener_hangup_test = 0;
int h265_only = 0;
int h265_camera = 0;
int call_duration = 3600;
int cloud_call = 0;

extern struct wx_audio_module audio_module;
extern struct wx_camera_module camera_module;
extern struct wx_crypto_module crypto_module;
extern struct wx_video_module_t video_module;
extern int64_t get_timestamp_us(void);

wx_error_t hal_get_module(const char* id, struct wx_module** module_out) {
  if (!strcmp(id, WX_AUDIO_MODULE_ID)) {
    *module_out = (struct wx_module*)&audio_module;
    return WXERROR_OK;
  }
  if (!strcmp(id, WX_CAMERA_MODULE_ID)) {
    *module_out = (struct wx_module*)&camera_module;
    return WXERROR_OK;
  }
  if (!strcmp(id, WX_CRYPTO_MODULE_ID)) {
    *module_out = (struct wx_module*)&crypto_module;
    return WXERROR_OK;
  }
  if (!strcmp(id, WX_VIDEO_MODULE_ID)) {
    *module_out = (struct wx_module*)&video_module;
    return WXERROR_OK;
  }

  return WXERROR_UNIMPLEMENTED;
}

wx_cloudvoip_session_status_t voip_status;

int64_t voip_status_2_ts = 0;

static void VoipSessionListenerStatus(wx_cloudvoip_session_t session,
                                      void* user_data,
                                      wx_cloudvoip_session_status_t status) {
  voip_status = status;

  if (status == 2) {
    voip_status_2_ts = get_timestamp_us();
  }

  printf("voip_status = %d\n", status);
}

static void VoipSessionListenerAvStatus(wx_cloudvoip_session_t session,
                                      void* user_data,
                                      wx_cloudvoip_session_av_status_t status) {
  printf("remote av status = %d\n", status);
}

static void VoipSessionListenerNetPoorStatus(wx_cloudvoip_session_t session,
                                      void* user_data,
                                      wx_cloudvoip_session_net_poor_status_t status) {
  printf("net poor status = %d\n", status);
}

static void VoipSessionListenerCmdmsgRecv(wx_cloudvoip_session_t session,
                                      void* user_data,
                                      wx_cloudvoip_session_cmdmsg_t msg) {
  printf("msg.len = %d\n", msg.len);
  printf("msg.msg = %s\n", msg.msg);
}

wx_cloudvoip_session_listener_t listener = {
    .common =
      {
        .tag = WX_CLOUDVOIP_SESSION_LISTENER_TAG,
        .size = sizeof(struct wx_cloudvoip_session_listener),
        .version = 0,
      },
    .status = VoipSessionListenerStatus,
    .av_status = VoipSessionListenerAvStatus,
    .net_poor_status = VoipSessionListenerNetPoorStatus,
    .cmd_msg_recv = VoipSessionListenerCmdmsgRecv,
};

static wx_error_t voip_init_config(void) {
  wx_init_config_t config = {0};

  config.common.tag = WX_INIT_CONFIG_TAG;
  config.common.size = sizeof(wx_init_config_t);
  config.common.version = 0;
  //config.log_dir = "/data";
  config.data_dir = "/data";
  config.h265_only = h265_only;

  /* video_landscape 宸插簾寮冿紝浣跨敤 subscribe_video_ratio 鏉ュ畾鍒跺楂樻瘮 */
  //config.video_landscape = !!landscape;
  config.subscribe_video_length = subscribe_video_length;
  config.subscribe_video_rotation = subscribe_video_rotation;
  config.subscribe_video_ratio = subscribe_video_ratio;
  config.subscribe_video_maxfps = subscribe_video_maxfps;

  wx_operation_t op = wx_init(&config, hal_get_module);
  return wx_operation_wait(op, 0);
}

static int do_hangup(wx_cloudvoip_hangup_reason_t reason) {
  wx_error_t ret = 0;
  wx_operation_t op;

  op = wx_cloudvoip_listener_hangup(
          appid, device_id, model_id, token, payload, reason);

  ret = wx_operation_wait(op, 0);
  if (ret != WXERROR_OK) {
    printf("wx_cloudvoip_listener_hangup fail, %d\n", ret);
    return EXIT_FAILURE;
  }

  return 0;
}

// 瀹氫箟鍙帴鍙楃殑閫夐」
static struct option long_options[] = {
  {"appid", required_argument, NULL, 'a'},
  {"device_id", required_argument, NULL, 'd'},
  {"model_id", required_argument, NULL, 'm'},
  {"server_token", required_argument, NULL, 't'},
  {"payload", required_argument, NULL, 'p'},
  {"subscribe_length", required_argument, NULL, 's'},
  {"subscribe_rotation", required_argument, NULL, 'v'},
  {"subscribe_maxfps", required_argument, NULL, 'f'},
  {"subscribe_ratio", required_argument, NULL, '9'},
  {"listener_hangup", required_argument, NULL, 'h'},
  {"roomid", required_argument, NULL, 'r'},
  {"session_key", required_argument, NULL, 'k'},
  {"h265_camera", required_argument, NULL, 'c'},
  {"h265_only", required_argument, NULL, '5'},
  {"call_duration", required_argument, NULL, '6'},
  {"cloud_call", required_argument, NULL, '7'},
  {NULL, 0, NULL, 0}
};

static void run_loop(void) {
  /* 鎸佺画 call_duration 绉掑悗鎸傛柇閫氳瘽 */
  int ts = 0;
  while ( voip_status == WX_CLOUDVOIP_SESSION_IDLE || voip_status == WX_CLOUDVOIP_SESSION_CALLING || voip_status == WX_CLOUDVOIP_SESSION_TALKING) {
    usleep(1000*100);
    ts += 100;
    if (ts >= call_duration*1000) {
      break;
    }
  }
}

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  wx_error_t ret = 0;
  wx_operation_t op;
  int opt;  
  
  while ((opt = getopt_long(argc, argv, "a:d:m:t:p:l:s:h:r:c:5:6:", long_options, NULL)) != -1) {  
    switch (opt) {  
    case 'a':  
      strcpy(appid, optarg);
      break;  
    case 'd':  
      strcpy(device_id, optarg);
      break;
    case 'm':
      strcpy(model_id, optarg);
      break;
    case 't':
      strcpy(token, optarg);
      break;
    case 'p':
      strcpy(payload, optarg);
      break;
    case 's':
      subscribe_video_length = atoi(optarg);
      break;
    case 'v':
      subscribe_video_rotation = atoi(optarg);
      break;
    case '9':
      subscribe_video_ratio = atoi(optarg);
      break;
    case 'f':
      subscribe_video_maxfps = atoi(optarg);
      break;
    case 'h':
      listener_hangup_test = atoi(optarg);
      break;
    case 'r':
      strcpy(roomid, optarg);
      break;
    case 'k':
      strcpy(session_key, optarg);
      break;
    case '5':
      h265_only = atoi(optarg);
      break;
    case '6':
      call_duration = atoi(optarg);
      break;
    case '7':
      cloud_call = atoi(optarg);
      break;
    case 'c':
      h265_camera = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Usage: %s --appid appid --device_id sn --model_id modelid --server_token token --payload payload [--hangup [0|1]] [--roomid roomid] [--h265_camera [0|1]] [--h265_only [0|1]] [--subscribe_length [320|480|640]] [--subscribe_rotation [1|2]] [--subscribe_maxfps [5-10]]\n", argv[0]);  
      exit(EXIT_FAILURE);  
    }  
  }

  printf("test args:\n");
  printf("\tappid: %s\n", appid);
  printf("\tdevice_id: %s\n", device_id);
  printf("\tmodel_id: %s\n", model_id);
  printf("\tserver_token: %s\n", token);
  printf("\tpayload: %s\n", payload);
  printf("\tsubscribe_length: %d\n", subscribe_video_length);
  printf("\tsubscribe_rotation: %d\n", subscribe_video_rotation);
  printf("\tsubscribe_ratio: %d\n", subscribe_video_ratio);
  printf("\tlistener_hangup: %d\n", listener_hangup_test);
  printf("\troomid: %s\n", roomid);
  printf("\tsession_key: %s\n", session_key);
  printf("\tcall_duration: %d\n", call_duration);
  printf("\tcloud_call: %d\n", cloud_call);
  printf("\th265_only: %d\n", h265_only);
  printf("\th265_camera: %d\n\n", h265_camera);

  if (appid[0] == 0 || device_id[0] == 0 || model_id[0] == 0 || token[0] == 0 || payload[0] == 0) {
    fprintf(stderr, "Usage: %s --appid appid --device_id sn --model_id modelid --server_token token --payload payload [--hangup [0|1]] [--roomid roomid] [--h265_camera [0|1]] [--h265_only [0|1]] [--subscribe_length [320|480|640]] [--subscribe_rotation [1|2]] [--subscribe_maxfps [5-10]]\n", argv[0]);
    exit(EXIT_FAILURE);  
  }

  /*
   * 婕旂ず濡備綍鍦ㄨ澶囧仛涓?listener 鏃舵寕鏂€氳瘽
   * 鎵嬫満灏忕▼搴忕浣跨敤 wmpfVoip.callDevice 鏃讹紝璁惧浣滀负 listener 鍙互浣跨敤 do_hangup 閲岀殑浠ｇ爜鎸傛柇瀵规柟 
   */
  if (listener_hangup_test == 1) {
    do_hangup(WX_CLOUDVOIP_HANGUP_REASON_MANUAL);
    return 0;
  }

  /*
   * 婕旂ず濡備綍鍔犲叆鎴块棿銆?   */
  wx_error_t init_ret = voip_init_config();
  if (init_ret != WXERROR_OK) {
    printf("voip init fail, %d\n", init_ret);
    return EXIT_FAILURE;
  }
  
  wx_cloudvoip_session_t session = NULL;

  if (cloud_call == 0) {
    op = wx_cloudvoip_session_join(WX_CLOUDVOIP_SESSION_VIDEO, &listener, NULL,
                                 appid, device_id, model_id, token, payload,
                                 &session);
  }
  else {
    op = wx_cloudvoip_session_cloud_call_join(WX_CLOUDVOIP_SESSION_VIDEO, &listener, NULL,
                                 appid, token, roomid, session_key, payload,
                                 &session);
  }

  ret = wx_operation_wait(op, 0);
  if (ret != WXERROR_OK) {
    printf("wx_cloudvoip_session_join fail, %d\n", ret);
 
    /* 杩欓噷涓轰粈涔堣鎸傛柇涓€涓嬶紵锛?*/
    op = wx_cloudvoip_session_hangup(session, WX_CLOUDVOIP_HANGUP_REASON_MANUAL);
    wx_operation_wait(op, 0);
    
    wx_cloudvoip_session_destroy(session);
    wx_stop();
    return EXIT_FAILURE;
  }

  start_audio_thread();
  start_camera_thread();

  run_loop();
  /*getchar();*/

  stop_audio_thread();
  stop_camera_thread();
  
  op = wx_cloudvoip_session_hangup(session, WX_CLOUDVOIP_HANGUP_REASON_MANUAL);
  printf("hangup ret %d\n", wx_operation_wait(op, 0) );

  wx_cloudvoip_session_destroy(session);
  wx_stop();
  return 0;
}


