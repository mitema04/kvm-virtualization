#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <stdint.h>
#include <pthread.h>

#define BUFFER_SIZE 256

extern char shared_buffer[BUFFER_SIZE];
extern uint32_t shared_count;  // koliko je trenutno validnih bajtova unutra
extern int buffer_ready;   // da li readeri mogu da citaju 
extern int readers_done;  // koliko je readera procitalo trenutnu verziju buffera 
extern int reader_count;
extern pthread_mutex_t buffer_mutex;
extern uint32_t buffer_version;
extern int transfer_finished;

extern int stop_all;
#endif