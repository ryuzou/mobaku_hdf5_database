#include <stdio.h>
#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "env_reader.h"
#include "db_credentials.h"

// 座標を格納する構造体
typedef struct {
    double lon;
    double lat;
} Coordinates;

// メッシュIDと座標のペアを格納する構造体
typedef struct {
    int mesh_id;
    Coordinates coords;
} MeshCoordPair;

// メッシュIDから座標へのマッピングを格納する構造体（動的配列を使用）
typedef struct {
    MeshCoordPair *pairs;
    size_t size;
    size_t capacity;
} MeshCoordMap;

// MeshCoordMapの初期化
void init_mesh_coord_map(MeshCoordMap *map) {
    map->pairs = NULL;
    map->size = 0;
    map->capacity = 0;
}

// MeshCoordMapに要素を追加
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

// MeshCoordMapの解放
void free_mesh_coord_map(MeshCoordMap *map) {
    free(map->pairs);
}

// 経度と緯度をメッシュIDに変換
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

// メッシュIDを中心の経度と緯度に変換
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
    coords.lat = (p + (double)q / 8.0 + (double)r / 80.0 + (double)s / 160.0) * 40.0 / 60.0 + (15.0 / 3600.0) / 2.0;
    coords.lon = 100.0 + (u + (double)v / 8.0 + (double)w / 80.0 + (double)x / 160.0) + (22.5 / 3600.0) / 2.0;
    return coords;
}

// データベースから人口データをクエリ
// （ここでは簡略化のため、結果の表示のみを行う）
void query_population_data(int* mesh_ids, size_t num_meshes, const char* db_uri) {
    if (num_meshes == 0) return;

    PGconn *conn = PQconnectdb(db_uri);
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

    size_t required_len = snprintf(NULL, 0, "SELECT /** Parallel(population_00000 8 hard) **/ * FROM population_00000 WHERE ");
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

    strcpy(query, "SELECT * FROM population_00000 WHERE ");
    for (size_t i = 0; i < num_meshes; i++) {
        snprintf(query + strlen(query), query_len - strlen(query), "mesh_id = %d%s", mesh_ids[i], (i < num_meshes - 1) ? " OR " : "");
    }
    snprintf(query + strlen(query), query_len - strlen(query), " ORDER BY datetime");



    PGresult *res = PQexec(conn, query);

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

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%s: %s\t", PQfname(res, j), PQgetvalue(res, i, j));
            const char *datetime_str = PQgetvalue(res, i, 1);
            struct tm tm;
            char *_result;

            _result = strptime(datetime_str, "%Y-%m-%d %H:%M:%S", &tm);
            if (_result == NULL) {
                fprintf(stderr, "日時の解析に失敗しました。\n");
                return EXIT_FAILURE;
            }

            // struct tmを使ってtime_tに変換
            time_t time_value = mktime(&tm);
            if (time_value == -1) {
                fprintf(stderr, "time_tへの変換に失敗しました。\n");
                return EXIT_FAILURE;
            }

            // 結果を表示
            printf("解析された日時: %s", ctime(&time_value));          }
        printf("\n");
    }

    PQclear(res);
    PQfinish(conn);
    free(query);
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
    snprintf(conninfo, sizeof(conninfo), "host=%s port=%s dbname=%s user=%s password=%s",
             creds->host, creds->port, creds->dbname, creds->user, creds->password);

    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }

    printf("Successfully connected to PostgreSQL!\n");

    int first_meshid = 5033;

    size_t num_meshes;
    int *mesh_ids = get_all_meshes_in_1st_mesh(first_meshid, &num_meshes);

    MeshCoordMap meshid_to_coords;
    init_mesh_coord_map(&meshid_to_coords);
    for(size_t i = 0; i < num_meshes; i++){
        Coordinates coords = meshid2lonlat_center(mesh_ids[i]);
        add_to_mesh_coord_map(&meshid_to_coords, mesh_ids[i], coords);
    }
    printf("Collecting %zu mesh IDs.\n", num_meshes);
    query_population_data(mesh_ids, num_meshes, conninfo);

    free(mesh_ids);
    free_mesh_coord_map(&meshid_to_coords);
    free_credentials(creds);

    return 0;
}