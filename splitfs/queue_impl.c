#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "queue_impl.h"

void queue_initialize(concurrent_queue *q)
{
  q->count = 0;
  pthread_mutex_init(&q->queue_lock, NULL);
  //pthread_spin_init(&q->queue_lock, PTHREAD_PROCESS_SHARED);
  q->front = NULL;
  q->rear = NULL;
}

int isempty(concurrent_queue *q)
{
  return (q->rear == NULL);
}

void enqueue(concurrent_queue *q, void *value)
{
  queue_node *tmp;
  tmp = calloc(1, sizeof(queue_node));
  tmp->data = value;
  tmp->next = NULL;
  //pthread_spin_lock(&q->queue_lock);
  pthread_mutex_lock(&q->queue_lock);
  if(!isempty(q))
    {
      q->rear->next = tmp;
      q->rear = tmp;
    }
  else
    {
      q->front = q->rear = tmp;
    }
  q->count++;
  //pthread_spin_unlock(&q->queue_lock);
  pthread_mutex_unlock(&q->queue_lock);
}

void *dequeue(concurrent_queue *q)
{
  queue_node *tmp;

  //pthread_spin_lock(&q->queue_lock);
  pthread_mutex_lock(&q->queue_lock);
  if (q->front == NULL) {
    q->rear = NULL;
    q->count = 0;
    //pthread_spin_unlock(&q->queue_lock);
    pthread_mutex_unlock(&q->queue_lock);
    return NULL;
  }
  void *n = q->front->data;
  tmp = q->front;
  q->front = q->front->next;
  q->count--;
  free(tmp);
  if (q->front == NULL) {
    q->rear = NULL;
    q->count = 0;
  }
  //pthread_spin_unlock(&q->queue_lock);
  pthread_mutex_unlock(&q->queue_lock);
  return(n);
}

void display(queue_node *head)
{
  if(head == NULL)
    {
      printf("NULL\n");
    }
  else
    {
      printf("%p\n", head -> data);
      display(head->next);
    }
}
