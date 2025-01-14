//
// Created by ryuzot on 25/01/06.
//
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <endian.h>

#include "env_reader.h"
#include "db_credentials.h"
#include "meshid_ops.h"
#include "fifioq.h"

#define NUM_PRODUCERS 32
#define NUM_ITEMS_PER_PRODUCER 30
#define MESHLIST_ONCE_LEN 16

#define NOW_ENTIRE_LEN_FOR_ONE_MESH 74160

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
    FIFOQueue *DataQueue;
    FIFOQueue * MeshlistQueue;
    const char * conninfo;
} ProducerObject;

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
    PGconn *conn = PQconnectdb(obj->conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        pthread_exit(NULL);
    }
    FIFOQueue *data_queue = obj->DataQueue;
    FIFOQueue *meshlist_queue = obj->MeshlistQueue;

    static const int64_t POSTGRES_EPOCH_IN_UNIX = 946684800LL;

    while (true) {
        MeshidList *meshid_list = (MeshidList*)dequeue(meshlist_queue);
        if (meshid_list == NULL) {
            break;
        }

        // mesh_idリストを文字列に展開
        char mesh_ids_str[4096] = ""; // 十分なバッファを確保
        for (int i = 0; i < meshid_list->meshid_number; i++) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%u", meshid_list->meshid_list[i]);
            strcat(mesh_ids_str, temp);
            if (i < meshid_list->meshid_number - 1) {
                strcat(mesh_ids_str, ",");
            }
        }

        char query[4096]; // 十分なバッファを確保
        snprintf(query, sizeof(query), "SELECT * FROM population_00000 WHERE mesh_id = ANY(ARRAY[%s]) ORDER BY datetime", mesh_ids_str);

        PGresult *res = PQexecParams(
            conn,
            query,
            0,       // nParams=0 (パラメータなし)
            NULL,    // paramTypes
            NULL,    // paramValues
            NULL,    // paramLengths
            NULL,    // paramFormats
            1        // resultFormat = 1 => バイナリ
    );

        int num_rows = PQntuples(res);
        int num_fields = PQnfields(res);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "SELECT failed: %s\n", PQerrorMessage(conn));
            PQclear(res);
            free_meshid_list(meshid_list);
            continue;
        }

        PQdataMatrix *qdata_matrix = (PQdataMatrix *)malloc(sizeof(PQdataMatrix));
        qdata_matrix->rows = NOW_ENTIRE_LEN_FOR_ONE_MESH;
        qdata_matrix->cols = meshid_list->meshid_number; // 取得するデータ数（mesh_idの数）
        qdata_matrix->data = (int *)malloc(sizeof(int) * qdata_matrix->rows * qdata_matrix->cols);
        memset(qdata_matrix->data, (uint32_t)0, sizeof(uint32_t) * qdata_matrix->rows * qdata_matrix->cols);
        if (qdata_matrix->data == NULL) {
            perror("malloc failed");
            exit(1);
        }

        int idx_mesh = -1;
        int idx_datetime = -1;
        int idx_population = -1;
        for (int k = 0; k < num_fields; k++) {
            const char* fieldName = PQfname(res, k);
            if (strcmp(fieldName, "mesh_id") == 0) {
                idx_mesh = k;
            } else if (strcmp(fieldName, "datetime") == 0) {
                idx_datetime = k;
            } else if (strcmp(fieldName, "population") == 0) {
                idx_population = k;
            }
        }
        if (idx_mesh == -1 || idx_datetime == -1 || idx_population == -1) {
            fprintf(stderr, "KEY ERROR");
            PQclear(res);
            free(qdata_matrix->data);
            free(qdata_matrix);
            free_meshid_list(meshid_list);
            continue;
        }

        cmph_t *local_hash = create_local_mph_from_int(meshid_list->meshid_list, meshid_list->meshid_number);

        for (int j = 0; j < num_rows && j < qdata_matrix->rows; j++) {
            int32_t mesh_id_netorder;
            memcpy(&mesh_id_netorder, PQgetvalue(res, j, idx_mesh), sizeof(int32_t));
            int meshid_value = (int)ntohl(mesh_id_netorder);

            int32_t pop_netorder;
            memcpy(&pop_netorder, PQgetvalue(res, j, idx_population), sizeof(int32_t));
            int population = (int)ntohl(pop_netorder);


            char *datetime_binary_ptr = PQgetvalue(res, j, idx_datetime);
            int datetime_binary_len = PQgetlength(res, j, idx_datetime);

            time_t datetime_binary_jst = pg_bin_timestamp_to_jst(datetime_binary_ptr, datetime_binary_len);

            int time_index = get_time_index_mobaku_datetime_from_time(datetime_binary_jst);
            int meshid_index = find_local_id(local_hash, meshid_value);
            qdata_matrix->data[time_index * qdata_matrix->cols + meshid_index] = population;    //row-major
        }
        cmph_destroy(local_hash);
        PQclear(res);
        enqueue(data_queue, qdata_matrix);
        free_meshid_list(meshid_list);
    }
    enqueue(data_queue, nullptr);
    PQfinish(conn);
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
        //printf("Consumed matrix (rows: %d, cols: %d)\n",m->rows, m->cols);

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
        printProgressBar(i * MESHLIST_ONCE_LEN, meshid_list_size);
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
    printf("\n");
    for (int k = 0; k < NUM_PRODUCERS; ++k) {
        enqueue(meshid_queue, nullptr);
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int cpulist[] = {0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23};  //HARDCODEING AWARE!!!
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

    FIFOQueue data_queue;
    FIFOQueue meshid_queue;
    init_queue(&data_queue);
    init_queue(&meshid_queue);

    pthread_attr_t attr;
    cpu_set_t cpuset;
    pthread_t producer_threads[NUM_PRODUCERS], consumer_thread, meshlist_producer_pthread;
    ProducerObject producer_objects[NUM_PRODUCERS];
    int target_cpu_core;

    // meshlist_producer スレッドの作成と affinity 設定
    pthread_attr_init(&attr);
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);    //HARDCODE AWARE!!!
    if (pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_attr_setaffinity_np failed for meshlist_producer");
    }
    if (pthread_create(&meshlist_producer_pthread, &attr, meshlist_producer, &meshid_queue) != 0) {
        perror("pthread_create failed for meshlist_producer");
        return 1;
    }
    pthread_attr_destroy(&attr);

    // producer スレッドの作成と affinity 設定
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_attr_init(&attr);
        CPU_ZERO(&cpuset);
        target_cpu_core = cpulist[i % (sizeof(cpulist) / sizeof(cpulist[0]))];    //HARDCODE AWARE!!!
        CPU_SET(target_cpu_core, &cpuset);
        if (pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset) != 0) {
            perror("pthread_attr_setaffinity_np failed for producer");
        }
        producer_objects[i].DataQueue = &data_queue;
        producer_objects[i].MeshlistQueue = &meshid_queue;
        producer_objects[i].conninfo = conninfo;
        if (pthread_create(&producer_threads[i], &attr, producer, &producer_objects[i]) != 0) {
            perror("pthread_create failed for producer");
            return 1;
        }
        pthread_attr_destroy(&attr);
    }

    // consumer スレッドの作成と affinity 設定
    pthread_attr_init(&attr);
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);    //HARDCODE AWARE!!!
    if (pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_attr_setaffinity_np failed for consumer");
    }
    if (pthread_create(&consumer_thread, &attr, consumer, &data_queue) != 0) {
        perror("pthread_create failed for consumer");
        return 1;
    }
    pthread_attr_destroy(&attr);

    pthread_join(meshlist_producer_pthread, NULL);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    pthread_join(consumer_thread, NULL);

    printf("All threads finished.\n");
    return 0;
}