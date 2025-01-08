#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <stdint.h>

#include "env_reader.h"
#include "db_credentials.h"

#define FETCH_SIZE 1000
#define QUEUE_SIZE 1024 // FIFOキューのサイズ

// ==== クエリ結果格納構造体 =====================================
typedef struct {
    uint32_t *result;
    int mesh_id; //どのメッシュIDのクエリ結果か
} QueryResult;

// ==== FIFOキュー ===============================================
typedef struct {
    QueryResult *data[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    sem_t sem_full; // キューが満杯かどうか
    sem_t sem_empty; // キューが空かどうか
} DataFIFOQueue;

// FIFOキューの初期化
void init_fifo_queue(DataFIFOQueue *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    sem_init(&queue->sem_full, 0, QUEUE_SIZE);
    sem_init(&queue->sem_empty, 0, 0);
}

// FIFOキューへのデータ追加
void enqueue(DataFIFOQueue *queue, QueryResult *result) {
    sem_wait(&queue->sem_full); // キューが満杯なら待機
    pthread_mutex_lock(&queue->mutex);
    queue->data[queue->tail] = result;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->sem_empty); // キューが空でなくなったことを通知
}

// FIFOキューからのデータ取り出し
QueryResult *dequeue(DataFIFOQueue *queue) {
    sem_wait(&queue->sem_empty); // キューが空なら待機
    pthread_mutex_lock(&queue->mutex);
    QueryResult *result = queue->data[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->sem_full); // キューに空きができたことを通知
    return result;
}

// FIFOキューの破棄
void destroy_fifo_queue(DataFIFOQueue *queue) {
    pthread_mutex_destroy(&queue->mutex);
    sem_destroy(&queue->sem_full);
    sem_destroy(&queue->sem_empty);
}


// スレッド関数
void *query_thread(void *arg) {
    struct thread_data {
        int mesh_id;
        const char *db_uri;
        DataFIFOQueue *queue;
    };
    struct thread_data *data = (struct thread_data*)arg;
    PGconn *conn = PQconnectdb(data->db_uri);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        pthread_exit(NULL); // スレッド終了
    }

    char query[256];
    snprintf(query, sizeof(query), "SELECT * FROM population_00000 WHERE mesh_id = %d ORDER BY datetime", data->mesh_id);

    PGresult *res = PQexec(conn, query);

    QueryResult *qr = (QueryResult *)malloc(sizeof(QueryResult));
    qr->result = res;
    qr->mesh_id = data->mesh_id;

    enqueue(data->queue, qr);
    PQfinish(conn);
    pthread_exit(NULL);
}

int* get_all_meshes_in_1st_mesh(int meshid_1, size_t *num_meshes) {
    *num_meshes = 8 * 8 * 10 * 10 * 4;
    int *mesh_ids = (int*)malloc(*num_meshes * sizeof(int));
    if (!mesh_ids) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    int index = 0;
    for (int q = 0; q < 8; q++) {
        for (int v = 0; v < 8; v++) {
            for (int r = 0; r < 10; r++) {
                for (int w = 0; w < 10; w++) {
                    for (int s = 0; s < 4; s++) {
                        int m = s + 1;
                        mesh_ids[index] = meshid_1 * 100000 + q * 10000 + v * 1000 + r * 100 + w * 10 + m;
                        index++;
                    }
                }
            }
        }
    }
    return mesh_ids;
}

int main(int argc, char* argv[]) {
    const char* env_filepath = ".env";
    if (argc > 1) {
        env_filepath = argv[1];
    }

    if (!load_env_from_file(env_filepath)) {
        fprintf(stderr, "Failed to load environment from %s\n", env_filepath);
        return 1;
    }
    DbCredentials *creds = get_db_credentials();

    if (!creds) {
        return 1;
    }

    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             creds->host, creds->port, creds->dbname, creds->user, creds->password);


    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }

    size_t num_meshes;
    int *mesh_ids = get_all_meshes_in_1st_mesh(first_meshid, &num_meshes);

    DataFIFOQueue queue;
    init_fifo_queue(&queue);

    pthread_t threads[num_meshes];
    struct thread_data thread_data_array[num_meshes];

    for (size_t i = 0; i < num_meshes; i++) {
        thread_data_array[i].mesh_id = mesh_ids[i];
        thread_data_array[i].db_uri = conninfo;
        thread_data_array[i].queue = &queue;
        pthread_create(&threads[i], NULL, query_thread, &thread_data_array[i]);
    }

    // メインスレッドで結果を処理
    for (size_t i = 0; i < num_meshes; i++) {
        QueryResult *qr = dequeue(&queue);
        PGresult *res = qr->result;
        int mesh_id = qr->mesh_id;

        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(res);
            int cols = PQnfields(res);
            for (int j = 0; j < rows; j++) {
                printf("Mesh ID: %d\t", mesh_id);
                for (int k = 0; k < cols; k++) {
                    printf("%s: %s\t", PQfname(res, k), PQgetvalue(res, j, k));
                }
                printf("\n");
            }
        } else {
            fprintf(stderr, "Query for mesh_id %d failed: %s\n", mesh_id, PQerrorMessage(res));
        }

        PQclear(res);
        free(qr);
    }

    for (size_t i = 0; i < num_meshes; i++) {
        pthread_join(threads[i], NULL);
    }

    destroy_fifo_queue(&queue);
    free(mesh_ids);
    free_credentials(creds);
    return 0;
}