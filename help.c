#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int64_t get_timestamp_us(void) {
  struct timespec tms;

  clock_gettime(CLOCK_REALTIME, &tms);

  return (tms.tv_sec * 1000000 + tms.tv_nsec/1000);
}