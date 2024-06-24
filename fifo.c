#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include "queue.h"

struct queue {
    void **buffer;
    int first;
    int back;
    int count;
    int size;
    pthread_mutex_t lock;
    sem_t full;
    sem_t empty;
};

queue_t *queue_new(int size) {
    queue_t *q = (queue_t *) malloc(sizeof(queue_t));
    if (!q) {
        return NULL;
    }

    q->buffer = (void **) malloc(size * sizeof(void *));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->size = size;
    q->first = 0;
    q->back = 0;
    q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    sem_init(&q->full, 0, size);
    sem_init(&q->empty, 0, 0);
    return q;
}

void queue_delete(queue_t **q) {
    if (!q || !*q) {
        return;
    }

    pthread_mutex_destroy(&(*q)->lock);
    sem_destroy(&(*q)->full);
    sem_destroy(&(*q)->empty);
    free((*q)->buffer);
    free(*q);
    *q = NULL;
}

bool queue_push(queue_t *q, void *elem) {
    if (!q) {
        return false;
    }

    sem_wait(&q->full);
    pthread_mutex_lock(&q->lock);

    q->buffer[q->back] = elem;
    q->back = (q->back + 1) % q->size;
    q->count++;

    pthread_mutex_unlock(&q->lock);
    sem_post(&q->empty);
    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    if (!q || !elem) {
        return false;
    }

    sem_wait(&q->empty);
    pthread_mutex_lock(&q->lock);

    *elem = q->buffer[q->first];
    q->first = (q->first + 1) % q->size;
    q->count--;

    pthread_mutex_unlock(&q->lock);
    sem_post(&q->full);

    return true;
}
