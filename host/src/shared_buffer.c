#include "shared_buffer.h"

char shared_buffer[BUFFER_SIZE];
uint32_t shared_count = 0;
int buffer_ready = 0;
int readers_done = 0;
int reader_count = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t buffer_version = 0;
int transfer_finished = 0;
int stop_all = 0;