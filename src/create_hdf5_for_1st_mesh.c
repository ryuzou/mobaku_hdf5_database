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

#include <hdf5.h>

#include "env_reader.h"
#include "db_credentials.h"
#include "meshid_ops.h"
#include "fifioq.h"

#define NUM_PRODUCERS 32
#define MESHLIST_ONCE_LEN 16

#define NOW_ENTIRE_LEN_FOR_ONE_MESH 74160
#define HDF5_DATETIME_CHUNK 8760 //365 * 24
#define HDF5_MESH_CHUNK 16

typedef struct {
    int rows;
    int cols;
    int *data;
    uint32_t meshid_start;
    cmph_t *local_hash;
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

    const char *stmtName = "select_population";
    const char *query = "SELECT mesh_id, datetime, population FROM population_00000 WHERE mesh_id = ANY($1) ORDER BY datetime";
    PGresult *prepRes = PQprepare(conn, stmtName, query, 1, NULL);
    if (PQresultStatus(prepRes) != PGRES_COMMAND_OK) {
        fprintf(stderr, "PQprepare failed: %s\n", PQerrorMessage(conn));
        PQclear(prepRes);
        PQfinish(conn);
        pthread_exit(NULL);
    }
    PQclear(prepRes);

    while (true) {
        MeshidList *meshid_list = (MeshidList*)dequeue(meshlist_queue);
        if (meshid_list == NULL) {
            break;
        }

        // mesh_idリストをPostgreSQLの配列形式の文字列に変換
        char mesh_ids_str[4096] = "{";
        for (int i = 0; i < meshid_list->meshid_number; ++i) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%u", meshid_list->meshid_list[i]);
            strcat(mesh_ids_str, temp);
            if (i < meshid_list->meshid_number - 1) {
                strcat(mesh_ids_str, ",");
            }
        }
        strcat(mesh_ids_str, "}");

        const char *paramValues[1] = {mesh_ids_str};
        int paramLengths[1] = {strlen(mesh_ids_str)};
        int paramFormats[1] = {0};

        PGresult *res = PQexecPrepared(conn, stmtName, 1, paramValues, paramLengths, paramFormats, 1);

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
        qdata_matrix->meshid_start = meshid_list->meshid_list[0];
        cmph_t *local_hash = create_local_mph_from_int(meshid_list->meshid_list, meshid_list->meshid_number);
        memset(qdata_matrix->data, 0, sizeof(int) * qdata_matrix->rows * qdata_matrix->cols);
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

        for (int j = 0; j < num_rows; j++) {
            int32_t meshid_value = ntohl(*((int32_t *)PQgetvalue(res, j, idx_mesh)));
            int32_t population = ntohl(*((int32_t *)PQgetvalue(res, j, idx_population)));

            char *datetime_binary_ptr = PQgetvalue(res, j, idx_datetime);
            int datetime_binary_len = PQgetlength(res, j, idx_datetime);

            time_t datetime_binary_jst = pg_bin_timestamp_to_jst(datetime_binary_ptr, datetime_binary_len);

            int time_index = get_time_index_mobaku_datetime_from_time(datetime_binary_jst);
            int meshid_index = find_local_id(local_hash, meshid_value);
            qdata_matrix->data[time_index * qdata_matrix->cols + meshid_index] = population;    //row-major
        }
        PQclear(res);
        enqueue(data_queue, qdata_matrix);
        free_meshid_list(meshid_list);
    }
    enqueue(data_queue, NULL);
    PQfinish(conn);
    pthread_exit(NULL);
}

typedef struct {
    FIFOQueue *queue;
    hid_t hdf5_file_id;
    uint32_t *all_meshes;
    int num_meshes;
} ConsumerArgs;

void *consumer(void *consumer_args) {
    ConsumerArgs *args = (ConsumerArgs *)consumer_args;
    FIFOQueue *q = args->queue;
    hid_t file_id = args->hdf5_file_id;
    int nulp_counter = 0;

    // データセットの作成準備
    hsize_t dims[2] = {NOW_ENTIRE_LEN_FOR_ONE_MESH, meshid_list_size};
    hid_t dataspace_id = H5Screate_simple(2, dims, NULL);
    if (dataspace_id < 0) {
        fprintf(stderr, "Failed to create dataspace in consumer\n");
        pthread_exit(NULL);
    }
    hid_t plist_id = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk_dims[2] = {HDF5_DATETIME_CHUNK, HDF5_MESH_CHUNK};
    H5Pset_chunk(plist_id, 2, chunk_dims);
    hid_t dataset_id = H5Dcreate(file_id, "population_data", H5T_NATIVE_INT, dataspace_id, H5P_DEFAULT, plist_id, H5P_DEFAULT);
    int fill_value = 0;
    herr_t status_fill = H5Pset_fill_value(plist_id, H5T_NATIVE_INT, &fill_value);
    if (status_fill < 0) {
        fprintf(stderr, "Failed to set fill value for HDF5 dataset\n");
        H5Pclose(plist_id);
        H5Sclose(dataspace_id);
        H5Fclose(file_id);
        pthread_exit(NULL);
    }
    H5Pclose(plist_id);
    if (dataset_id < 0) {
        fprintf(stderr, "Failed to create dataset in consumer\n");
        H5Sclose(dataspace_id);
        pthread_exit(NULL);
    }

    int processed_meshes = 0; // プログレスバー用カウンタ (処理済みメッシュ数)
    int total_meshes = args->num_meshes; // プログレスバー用合計メッシュ数
    cmph_t *local_hash = create_local_mph_from_int(args->all_meshes, total_meshes);

    while (true) {
        PQdataMatrix *m = dequeue(q);
        if (m == NULL) {
            nulp_counter++;
            if (nulp_counter == NUM_PRODUCERS) {
                break;
            }
            continue;
        }

        processed_meshes += m->cols;
        printProgressBar(processed_meshes, total_meshes);
        hsize_t offset[2];
        hsize_t count[2];

        offset[0] = 0; // 常に先頭から
        int global_mesh_index = find_local_id(local_hash, m->meshid_start);
        if (global_mesh_index == -1) {
            fprintf(stderr, "Error: mesh ID %u not found in global list.\n", m->meshid_start);
            free_pqdata_matrix(m);
            continue;
        }
        offset[1] = global_mesh_index; // 書き込み開始のメッシュID

        count[0] = m->rows;
        count[1] = m->cols;

        hid_t memspace_id = H5Screate_simple(2, count, NULL);
        hid_t dataset_space_id = H5Dget_space(dataset_id);
        hsize_t selected_offset[2] = {offset[0], offset[1]};
        hsize_t selected_count[2] = {count[0], count[1]};
        H5Sselect_hyperslab(dataset_space_id, H5S_SELECT_SET, selected_offset, NULL, selected_count, NULL);

        herr_t status = H5Dwrite(dataset_id, H5T_NATIVE_INT, memspace_id, dataset_space_id, H5P_DEFAULT, m->data);
        if (status < 0) {
            fprintf(stderr, "Failed to write data to HDF5 dataset\n");
        }

        H5Sclose(memspace_id);
        H5Sclose(dataset_space_id);
        free_pqdata_matrix(m);
    }

    printf("\n"); // プログレスバー改行

    // HDF5 リソースをクローズ
    H5Dclose(dataset_id);
    H5Sclose(dataspace_id);
    H5Fclose(file_id);

    pthread_exit(NULL);
}

typedef struct {
    FIFOQueue *meshid_queue;
    uint32_t *all_meshes;
    int num_meshes;
} MeshlistProducerArgs;

void *meshlist_producer(void *arg) {
    MeshlistProducerArgs *args = (MeshlistProducerArgs *)arg;
    FIFOQueue *meshid_queue = args->meshid_queue;
    uint32_t *all_meshes = args->all_meshes;
    int mesh_count = args->num_meshes;
    int i;

    for (i = 0; i < (int)(mesh_count / MESHLIST_ONCE_LEN); ++i) {
        uint32_t *meshid_once_list = (uint32_t *)malloc(MESHLIST_ONCE_LEN * sizeof(uint32_t));
        MeshidList *m = (MeshidList *)malloc(sizeof(MeshidList));
        m->meshid_number = MESHLIST_ONCE_LEN;
        for (int j = 0; j < MESHLIST_ONCE_LEN; ++j) {
            meshid_once_list[j] = all_meshes[i * MESHLIST_ONCE_LEN + j];
        }
        m->meshid_list = meshid_once_list;
        enqueue(meshid_queue, m);
    }
    if (mesh_count % MESHLIST_ONCE_LEN != 0) {
        uint32_t *meshid_once_list = (uint32_t *)malloc((mesh_count % MESHLIST_ONCE_LEN) * sizeof(uint32_t));
        MeshidList *m = (MeshidList *)malloc(sizeof(MeshidList));
        m->meshid_number = (mesh_count % MESHLIST_ONCE_LEN);
        for (int j = 0; j < mesh_count % MESHLIST_ONCE_LEN; ++j) {
            meshid_once_list[j] = all_meshes[i * MESHLIST_ONCE_LEN + j];
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
    int mesh1st;
    if (argc > 2) {
        mesh1st = atoi(argv[2]);
    } else {
        fprintf(stderr, "Usage: %s <env_file> <mesh1st> <output_file>\n", argv[0]);
        return 1;
    }
    int NUM_MESHES_1ST = 25600;
    int *all_meshes = get_all_meshes_in_1st_mesh(mesh1st, NUM_MESHES_1ST);

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

    // HDF5 ファイルを作成
    const char* hdf5_filepath;
    if (argc > 3) {
        hdf5_filepath = argv[3];
    } else {
        fprintf(stderr, "Usage: %s <env_file> <mesh1st> <output_file>\n", argv[0]);
        return 1;
    }
    hid_t file_id = H5Fcreate(hdf5_filepath, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        fprintf(stderr, "Failed to create HDF5 file: %s\n", hdf5_filepath);
        return 1;
    }

    // meshid_list メタデータの書き込み
    hsize_t meshid_list_dims[1] = {NUM_MESHES_1ST};
    hid_t meshid_list_space_id = H5Screate_simple(1, meshid_list_dims, NULL);
    hid_t meshid_list_dataset_id = H5Dcreate(file_id, "meshid_list", H5T_NATIVE_UINT32, meshid_list_space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (meshid_list_dataset_id < 0) {
        fprintf(stderr, "Failed to create meshid_list dataset\n");
        H5Sclose(meshid_list_space_id);
        H5Fclose(file_id);
        return 1;
    }
    H5Dwrite(meshid_list_dataset_id, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, all_meshes);
    H5Dclose(meshid_list_dataset_id);
    H5Sclose(meshid_list_space_id);

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
    MeshlistProducerArgs mpl_args = {
        .meshid_queue = &meshid_queue,
        .all_meshes = all_meshes,
        .num_meshes = NUM_MESHES_1ST
    };
    if (pthread_create(&meshlist_producer_pthread, &attr, meshlist_producer, &mpl_args) != 0) {
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

    ConsumerArgs consumer_args;
    consumer_args.queue = &data_queue;
    consumer_args.hdf5_file_id = file_id;
    consumer_args.num_meshes = NUM_MESHES_1ST;
    consumer_args.all_meshes = all_meshes;
    if (pthread_create(&consumer_thread, &attr, consumer, &consumer_args) != 0) {
        perror("pthread_create failed for consumer");
        // HDF5 ファイルをクローズ (エラー処理)
        H5Fclose(file_id);
        return 1;
    }
    pthread_attr_destroy(&attr);

    pthread_join(meshlist_producer_pthread, NULL);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    pthread_join(consumer_thread, NULL);

    printf("All threads finished.\n");

    free(all_meshes);
    return 0;
}