#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "env_reader.h"
#include "db_credentials.h"

// ==== 時間計測用ユーティリティ =================================
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // 秒 + ナノ秒(小数点)
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

// ==== 座標を格納する構造体 ========================================
typedef struct {
    double lon;
    double lat;
} Coordinates;

// ==== メッシュIDと座標のペアを格納する構造体 ========================
typedef struct {
    int mesh_id;
    Coordinates coords;
} MeshCoordPair;

// ==== メッシュIDから座標へのマッピングを格納する構造体（動的配列） ==
typedef struct {
    MeshCoordPair *pairs;
    size_t size;
    size_t capacity;
} MeshCoordMap;

// ==== MeshCoordMapの初期化 =========================================
void init_mesh_coord_map(MeshCoordMap *map) {
    map->pairs = NULL;
    map->size = 0;
    map->capacity = 0;
}

// ==== MeshCoordMapに要素を追加 ======================================
void add_to_mesh_coord_map(MeshCoordMap *map, int mesh_id, Coordinates coords) {
    if (map->size >= map->capacity) {
        map->capacity = (map->capacity == 0) ? 4 : map->capacity * 2;
        MeshCoordPair *new_pairs = (MeshCoordPair *)realloc(map->pairs, map->capacity * sizeof(MeshCoordPair));
        if (!new_pairs) {
            perror("realloc failed");
            exit(EXIT_FAILURE);
        }
        map->pairs = new_pairs;
    }
    map->pairs[map->size].mesh_id = mesh_id;
    map->pairs[map->size].coords = coords;
    map->size++;
}

// ==== MeshCoordMapの解放 ===========================================
void free_mesh_coord_map(MeshCoordMap *map) {
    free(map->pairs);
    map->pairs = NULL;  // 念のためNULLクリア
}

// ==== 経度と緯度をメッシュIDに変換 ==================================
int lonlat2meshid(double lon, double lat) {
    double p, a, q, b, r, c, s, d;
    double u, f, v, g, w, h, x, i;

    p = modf(lat * 60.0 / 40.0, &a);
    q = modf(a * 8.0, &b);
    r = modf(b * 10.0, &c);
    s = modf(c * 2.0, &d);

    u = modf(lon - 100.0, &f);
    v = modf(f * 8.0, &g);
    w = modf(g * 10.0, &h);
    x = modf(h * 2.0, &i);

    int m = (int)(s * 2 + x + 1);
    return (int)(a * 10000000.0 + u * 100000.0 + b * 10000.0 + v * 1000.0 + c * 100.0 + w * 10.0 + m);
}

// ==== メッシュIDを中心の経度と緯度に変換 ============================
Coordinates meshid2lonlat_center(int meshid) {
    int m = meshid % 10;
    meshid /= 10;
    int w = meshid % 10;
    meshid /= 10;
    int r = meshid % 10;
    meshid /= 10;
    int v = meshid % 10;
    meshid /= 10;
    int q = meshid % 10;
    meshid /= 10;
    int u = meshid % 100;
    meshid /= 100;
    int p = meshid;

    int s = (m - 1) / 2;
    int x = (m - 1) % 2;
    Coordinates coords;
    coords.lat = (p + (double)q / 8.0 + (double)r / 80.0 + (double)s / 160.0) * 40.0 / 60.0
                 + (15.0 / 3600.0) / 2.0;
    coords.lon = 100.0 + (u + (double)v / 8.0 + (double)w / 80.0 + (double)x / 160.0)
                 + (22.5 / 3600.0) / 2.0;
    return coords;
}

// ==== データベースから人口データをクエリ ============================
void query_population_data(int* mesh_ids, size_t num_meshes, const char* db_uri) {
    if (num_meshes == 0) return;

    double t0 = get_time_sec();
    PGconn *conn = PQconnectdb(db_uri);
    double t1 = get_time_sec();
    printf("[query_population_data] PQconnectdb took: %.6f sec\n", t1 - t0);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    // 動的にクエリ文字列を構築
    size_t query_len = 256; // 初期バッファサイズ
    char *query = (char *)malloc(query_len);
    if (!query) {
        perror("malloc failed");
        PQfinish(conn);
        return;
    }
    query[0] = '\0'; // 初期化

    // 必要な文字列長を計算
    size_t required_len = snprintf(NULL, 0,
        "SELECT /** Parallel(population_00000 8 hard) **/ * FROM population_00000 WHERE ");
    for (size_t i = 0; i < num_meshes; i++) {
        required_len += snprintf(NULL, 0, "mesh_id = %d%s", mesh_ids[i], (i < num_meshes - 1) ? " OR " : "");
    }
    required_len += snprintf(NULL, 0, " ORDER BY datetime");

    if (required_len + 1 > query_len) {
        query_len = required_len + 1;
        char *new_query = (char *)realloc(query, query_len);
        if (!new_query) {
            perror("realloc failed");
            free(query);
            PQfinish(conn);
            return;
        }
        query = new_query;
    }

    // 実際のクエリ文字列を作成
    strcpy(query, "SELECT /** Parallel(population_00000 8 hard) **/ * FROM population_00000 WHERE ");
    for (size_t i = 0; i < num_meshes; i++) {
        snprintf(query + strlen(query), query_len - strlen(query),
                 "mesh_id = %d%s", mesh_ids[i], (i < num_meshes - 1) ? " OR " : "");
    }
    snprintf(query + strlen(query), query_len - strlen(query), " ORDER BY datetime");

    double t2 = get_time_sec();
    printf("[query_population_data] Build query took: %.6f sec\n", t2 - t1);

    // クエリ実行
    PGresult *res = PQexec(conn, query);
    double t3 = get_time_sec();
    printf("[query_population_data] PQexec took: %.6f sec\n", t3 - t2);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        free(query);
        return;
    }

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    printf("Number of rows: %d\n", rows);
    printf("Number of columns: %d\n", cols);
    int tmp = 0;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%s: %s\t", PQfname(res, j), PQgetvalue(res, i, j));
        }
        volatile const char *datetime_str = PQgetvalue(res, i, 1);
        struct tm tm;
        char *_result;

        _result = strptime(datetime_str, "%Y-%m-%d %H:%M:%S", &tm);
        if (_result == NULL) {
            fprintf(stderr, "日時の解析に失敗しました。\n");
        }
        time_t time_value = mktime(&tm);
        if (time_value == -1) {
            fprintf(stderr, "time_tへの変換に失敗しました。\n");
        }

        // 結果を表示
        printf("解析された日時: %s", ctime(&time_value));
        printf("\n");
        tmp++;
        if (tmp >= 10) {
            break;
        }
    }

    double t4 = get_time_sec();
    printf("[query_population_data] Looping results took: %.6f sec\n", t4 - t3);

    PQclear(res);
    double t5 = get_time_sec();
    printf("[query_population_data] PQclear took: %.6f sec\n", t5 - t4);

    PQfinish(conn);
    double t6 = get_time_sec();
    printf("[query_population_data] PQfinish took: %.6f sec\n", t6 - t5);

    free(query);
    double t7 = get_time_sec();
    printf("[query_population_data] free(query) took: %.6f sec\n", t7 - t6);
}

// ==== 1次メッシュIDに含まれる全メッシュIDを取得 =====================
int* get_all_meshes_in_1st_mesh(int meshid_1, size_t *num_meshes) {
    *num_meshes = 8 * 8 * 10 * 10 * 4;  // 25,600
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
                        mesh_ids[index] = meshid_1 * 100000
                                          + q * 10000
                                          + v * 1000
                                          + r * 100
                                          + w * 10
                                          + m;
                        index++;
                    }
                }
            }
        }
    }
    return mesh_ids;
}

// ==== メイン関数 ====================================================
int main(int argc, char* argv[]) {
    double main_start = get_time_sec();

    const char* env_filepath = ".env";
    if (argc > 1) {
        env_filepath = argv[1];
    }

    double t0 = get_time_sec();
    if (!load_env_from_file(env_filepath)) {
        fprintf(stderr, "Failed to load environment from %s\n", env_filepath);
        return 1;
    }
    double t1 = get_time_sec();
    printf("[main] load_env_from_file took: %.6f sec\n", t1 - t0);

    t0 = get_time_sec();
    DbCredentials *creds = get_db_credentials();
    t1 = get_time_sec();
    printf("[main] get_db_credentials took: %.6f sec\n", t1 - t0);

    if (!creds) {
        return 1;
    }

    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             creds->host, creds->port, creds->dbname, creds->user, creds->password);

    // DB接続試験
    t0 = get_time_sec();
    PGconn *conn = PQconnectdb(conninfo);
    t1 = get_time_sec();
    printf("[main] PQconnectdb took: %.6f sec\n", t1 - t0);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }
    PQfinish(conn);

    printf("Successfully connected to PostgreSQL!\n");

    int first_meshid = 5033;

    // メッシュID一覧を取得
    t0 = get_time_sec();
    size_t num_meshes;
    int *mesh_ids = get_all_meshes_in_1st_mesh(first_meshid, &num_meshes);
    t1 = get_time_sec();
    printf("[main] get_all_meshes_in_1st_mesh took: %.6f sec\n", t1 - t0);

    // 座標計算
    t0 = get_time_sec();
    MeshCoordMap meshid_to_coords;
    init_mesh_coord_map(&meshid_to_coords);
    for(size_t i = 0; i < num_meshes; i++){
        Coordinates coords = meshid2lonlat_center(mesh_ids[i]);
        add_to_mesh_coord_map(&meshid_to_coords, mesh_ids[i], coords);
    }
    t1 = get_time_sec();
    printf("[main] meshid2lonlat_center & add_to_mesh_coord_map took: %.6f sec\n", t1 - t0);

    printf("Collecting %zu mesh IDs.\n", num_meshes);

    // 人口データクエリ
    t0 = get_time_sec();
    query_population_data(mesh_ids, num_meshes, conninfo);
    t1 = get_time_sec();
    printf("[main] query_population_data took: %.6f sec\n", t1 - t0);

    // メモリ解放
    t0 = get_time_sec();
    free(mesh_ids);
    free_mesh_coord_map(&meshid_to_coords);
    free_credentials(creds);
    t1 = get_time_sec();
    printf("[main] free(mesh_ids), free_mesh_coord_map, free_credentials took: %.6f sec\n", t1 - t0);

    double main_end = get_time_sec();
    printf("[main] Total execution time: %.6f sec\n", main_end - main_start);

    return 0;
}
