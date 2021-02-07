#ifndef SPLITFS_STAGING_QUEUE_H
#define SPLITFS_STAGEING_QUEUE_H

#include <stdatomic.h>

struct queue_node
{
  volatile void *data;
  volatile struct queue_node *next;
};
typedef struct queue_node queue_node;

struct queue
{
  //volatile pthread_spinlock_t queue_lock;
  volatile pthread_mutex_t queue_lock;
  volatile int count;
  volatile queue_node *front;
  volatile queue_node *rear;
};
typedef struct queue concurrent_queue;

void queue_initialize(concurrent_queue *q);
int isempty(concurrent_queue *q);
void enqueue(concurrent_queue *q, void *value);
void *dequeue(concurrent_queue *q);
void display(queue_node *head);

volatile concurrent_queue *append_staging_mmap_queue;
volatile concurrent_queue *over_staging_mmap_queue;

#endif
