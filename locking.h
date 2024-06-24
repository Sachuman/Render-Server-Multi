

#pragma once

#include <stdint.h>


typedef struct rwlock rwlock_t;

typedef enum { READERS, WRITERS, N_WAY } PRIORITY;



rwlock_t *rwlock_new(PRIORITY p, uint32_t n);

void rwlock_delete(rwlock_t **rw);


void reader_lock(rwlock_t *rw);


void reader_unlock(rwlock_t *rw);


void writer_lock(rwlock_t *rw);


void writer_unlock(rwlock_t *rw);
