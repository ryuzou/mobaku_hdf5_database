//
// Created by ryuzot on 25/01/06.
//
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include "meshid_ops.h"

#define NUM_PRODUCERS 2
#define NUM_ITEMS_PER_PRODUCER 30
#define QUEUE_SIZE 100
#define MESHLIST_ONCE_LEN 16


typedef struct {
    int rows;
    int cols;
    int *data;
} PQdataMatrix;

typedef struct {
    int meshid_number;
    uint32_t *meshid_list;
} MeshidList;

typedef struct {
    void *queue[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    sem_t full;
    sem_t empty;
} FIFOQueue;

typedef struct {
    FIFOQueue *DataQueue;
    FIFOQueue * MeshlistQueue;
} ProducerObject;

void init_queue(FIFOQueue *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->full, 0, QUEUE_SIZE);
    sem_init(&q->empty, 0, 0);
}

void enqueue(FIFOQueue *q, void *data) {
    sem_wait(&q->full);
    pthread_mutex_lock(&q->mutex);
    q->queue[q->tail] = data;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->empty);
}

void *dequeue(FIFOQueue *q) {
    sem_wait(&q->empty);
    pthread_mutex_lock(&q->mutex);
    void *data = q->queue[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->full);
    return data;
}

// データ解放関数
void free_pqdata_matrix(void *data) {
    PQdataMatrix *m = (PQdataMatrix *)data;
    free(m->data);
    free(m);
}

void free_meshid_list(void *data) {
    MeshidList *ml = (MeshidList *)data;
    free(ml->meshid_list);
    free(ml);
}


void *producer(void *arg) {
    ProducerObject *obj = (ProducerObject *)arg;
    FIFOQueue *data_queue = obj->DataQueue;
    FIFOQueue * meshlist_queue = obj->MeshlistQueue;
    while (true) {
        MeshidList *meshid_list = dequeue(meshlist_queue);
        if (meshid_list == nullptr) {
            break;
        }
        PQdataMatrix *m = (PQdataMatrix *)malloc(sizeof(PQdataMatrix));
        m->rows = 100;
        m->cols = meshid_list->meshid_number;
        m->data = (int *)malloc(sizeof(int) * m->rows * m->cols);
        if (m->data == NULL) {
            perror("malloc failed");
            exit(1);
        }
        for (int j = 0; j < m->rows; j++) {
            for (int k = 0; k < m->cols; k++) {
                m->data[j * m->cols + k] = 1234;
            }
        }
        enqueue(data_queue, m);
        free_meshid_list(meshid_list);
    }
    enqueue(data_queue, nullptr);

    pthread_exit(NULL);
}

void *consumer(void *arg) {
    FIFOQueue *q = (FIFOQueue *)arg;
    int nulp_counter = 0;
    while (true) {
        PQdataMatrix *m = dequeue(q);
        if (m == nullptr) {
            nulp_counter++;
            if (nulp_counter == NUM_PRODUCERS) {
                break;
            }
            continue;
        }
        printf("Consumed matrix (rows: %d, cols: %d)\n",m->rows, m->cols);
        free_pqdata_matrix(m);
    }
    pthread_exit(NULL);
}

void *meshlist_producer(void *arg) {
    FIFOQueue *meshid_queue = (FIFOQueue *)arg;
    int i;
    for (i = 0; i < (int)(meshid_list_size / MESHLIST_ONCE_LEN); ++i) {
        uint32_t *meshid_once_list = (int *)malloc(MESHLIST_ONCE_LEN * sizeof(uint32_t));
        MeshidList *m = (MeshidList *)malloc(sizeof(MeshidList));
        m->meshid_number = MESHLIST_ONCE_LEN;
        for (int j = 0; j < MESHLIST_ONCE_LEN; ++j) {
            meshid_once_list[j] = meshid_list[i * MESHLIST_ONCE_LEN + j];
        }
        m->meshid_list = meshid_once_list;
        enqueue(meshid_queue, m);

        printf("Progress: %d / %lu\n",
               i * MESHLIST_ONCE_LEN,
               meshid_list_size);
    }
    if (meshid_list_size % MESHLIST_ONCE_LEN != 0) {
        uint32_t *meshid_once_list = (int *)malloc((meshid_list_size % MESHLIST_ONCE_LEN) * sizeof(uint32_t));
        MeshidList *m = (MeshidList *)malloc(sizeof(MeshidList));
        m->meshid_number = (meshid_list_size % MESHLIST_ONCE_LEN);
        for (int j = 0; j < meshid_list_size % MESHLIST_ONCE_LEN; ++j) {
            meshid_once_list[j] = meshid_list[i * MESHLIST_ONCE_LEN + j];
        }
        m->meshid_list = meshid_once_list;
        enqueue(meshid_queue, m);
    }
    for (int k = 0; k < NUM_PRODUCERS; ++k) {
        enqueue(meshid_queue, nullptr);
    }

    pthread_exit(NULL);
}


int main() {
    FIFOQueue data_queue;
    FIFOQueue meshid_queue;
    init_queue(&data_queue);
    init_queue(&meshid_queue);


    pthread_t producer_threads[NUM_PRODUCERS], consumer_thread, meshlist_producer_pthread;

    ProducerObject producer_objects[NUM_PRODUCERS];

    pthread_create(&meshlist_producer_pthread, NULL, meshlist_producer, &meshid_queue);
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_objects[i].DataQueue = &data_queue;
        producer_objects[i].MeshlistQueue = &meshid_queue;
        pthread_create(&producer_threads[i], NULL, producer, &producer_objects[i]);
    }

    pthread_create(&consumer_thread, NULL, consumer, &data_queue);

    pthread_join(meshlist_producer_pthread, NULL);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    pthread_join(consumer_thread, NULL);

    printf("All threads finished.\n");
    return 0;
}