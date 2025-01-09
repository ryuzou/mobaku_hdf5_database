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
#include "meshid_ops.h"
#include "fifioq.h"

#define NUM_PRODUCERS 32
#define NUM_ITEMS_PER_PRODUCER 30
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

        PGresult *res = PQexec(conn, query);

        int num_rows = PQntuples(res);
        int num_fields = PQnfields(res);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "SELECT failed: %s\n", PQerrorMessage(conn));
            PQclear(res);
            free_meshid_list(meshid_list);
            continue;
        }

        PQdataMatrix *m = (PQdataMatrix *)malloc(sizeof(PQdataMatrix));
        m->rows = num_rows;
        m->cols = meshid_list->meshid_number; // 取得するデータ数（mesh_idの数）
        m->data = (int *)malloc(sizeof(int) * m->rows * m->cols);
        if (m->data == NULL) {
            perror("malloc failed");
            exit(1);
        }
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "SELECT failed: %s\n", PQerrorMessage(conn));
            PQclear(res);
            free(m->data);
            free(m);
            free_meshid_list(meshid_list);
            continue;
        }

        int data_index = 0;

        for (int j = 0; j < num_rows && j < m->rows; j++) {
            for(int mesh_index = 0; mesh_index < meshid_list->meshid_number; mesh_index++){
                bool found = false;
                for(int k = 0; k < num_fields; k++){
                    if(strcmp(PQfname(res, k), "mesh_id") == 0){
                        char *mesh_id_val = PQgetvalue(res, j, k);
                        if(mesh_id_val != NULL && atoi(mesh_id_val) == meshid_list->meshid_list[mesh_index]){
                            found = true;
                            break;
                        }
                    }
                }
                if(found){
                  for(int k = 0; k < num_fields; k++){
                    if(strcmp(PQfname(res, k), "datetime") == 0){
                        char *value = PQgetvalue(res, j, k);
                        if (value != NULL) {
                            m->data[j * m->cols + mesh_index] = atoi(value);
                        } else {
                            m->data[j * m->cols + mesh_index] = 0;
                        }
                        break;
                    }
                  }
                } else{
                    m->data[j * m->cols + mesh_index] = 0;
                }
            }
        }
        PQclear(res);
        enqueue(data_queue, m);
        free_meshid_list(meshid_list);
    }
    enqueue(data_queue, NULL);
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
        producer_objects[i].conninfo = conninfo;
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