#include <stdlib.h>
#include <pthread.h>
#include "rwlock.h"

struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t go_reader;
    pthread_cond_t go_writer;
    int readersnwaywriters;
    int iamwriter;
    int writerwait;
    int iamreader;
    int readerswait;
    PRIORITY priority;
    int n;
};

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rw = (rwlock_t *) malloc(sizeof(rwlock_t));
    if (!rw)
        return NULL;

    pthread_mutex_init(&rw->lock, NULL);
    pthread_cond_init(&rw->go_writer, NULL);

    pthread_cond_init(&rw->go_reader, NULL);

    rw->iamreader = 0;
    rw->iamwriter = 0;
    rw->writerwait = 0;
    rw->priority = p;
    rw->readerswait = 0;
    rw->readersnwaywriters = 0;
    rw->n = n;

    return rw;
}

void rwlock_delete(rwlock_t **rw) {
    if (!rw || !*rw) {
        return;
    }

    pthread_mutex_destroy(&(*rw)->lock);
    pthread_cond_destroy(&(*rw)->go_writer);

    pthread_cond_destroy(&(*rw)->go_reader);
    free(*rw);
    *rw = NULL;
}

void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);

    rw->readerswait++;
    while ((rw->priority == N_WAY && rw->writerwait > 0 && rw->readersnwaywriters >= rw->n)
           || rw->iamwriter > 0 || (rw->priority == WRITERS && rw->writerwait > 0)) {
        pthread_cond_wait(&rw->go_reader, &rw->lock);
    }
    rw->readerswait--;
    rw->iamreader++;
    if (rw->priority == N_WAY) {
        rw->readersnwaywriters++;
    }

    pthread_mutex_unlock(&rw->lock);
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);

    rw->iamreader--;
    if (rw->iamreader == 0) {
        pthread_cond_signal(&rw->go_writer);
    }

    pthread_mutex_unlock(&rw->lock);
}

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);

    rw->writerwait++;
    while ((rw->priority == N_WAY && rw->readersnwaywriters < rw->n && rw->readerswait > 0)
           || rw->iamwriter > 0 || rw->iamreader > 0) {

        pthread_cond_wait(&rw->go_writer, &rw->lock);
    }
    rw->writerwait--;
    rw->iamwriter++;
    rw->readersnwaywriters = 0;

    pthread_mutex_unlock(&rw->lock);
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);

    rw->iamwriter--;
    if (rw->priority == READERS) {
        if (rw->readerswait > 0) {
            pthread_cond_broadcast(&rw->go_reader);
        } else if (rw->writerwait > 0) {
            pthread_cond_signal(&rw->go_writer);
        }
    } else if (rw->priority == N_WAY) {
        if (rw->readerswait > 0 && rw->readersnwaywriters < rw->n) {
            pthread_cond_broadcast(&rw->go_reader);
        } else if (rw->writerwait > 0) {
            pthread_cond_signal(&rw->go_writer);
        }
    } else {
        if (rw->writerwait > 0) {
            pthread_cond_signal(&rw->go_writer);
        } else if (rw->readerswait > 0) {
            pthread_cond_broadcast(&rw->go_reader);
        }
    }

    pthread_mutex_unlock(&rw->lock);
}
