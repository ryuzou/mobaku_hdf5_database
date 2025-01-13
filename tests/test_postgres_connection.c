#define _GNU_SOURCE

#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>   // ntohl
#include <libpq-fe.h>

#include "env_reader.h"
#include "db_credentials.h"
#include "meshid_ops.h"

// テキスト形式で取得した "YYYY-MM-DD HH:MM:SS" などを time_t (ローカルタイム) に変換する
static time_t parse_text_timestamp(const char *datetime_str)
{
    // 例: 文字列 "2025-01-09 14:15:16" を想定
    // ミリ秒やタイムゾーンが付く場合はフォーマット修正が必要
    struct tm tm_buf;
    memset(&tm_buf, 0, sizeof(tm_buf));

    // strptime でパース (ローカルタイム扱い)
    if (!strptime(datetime_str, "%Y-%m-%d %H:%M:%S", &tm_buf)) {
        fprintf(stderr, "Failed to parse datetime_str='%s'\n", datetime_str);
        return (time_t)-1;
    }

    // mktime はローカルタイムとして解釈
    time_t t = mktime(&tm_buf);
    return t;
}

int main(int argc, char* argv[])
{
    // -- 環境変数読み込み (省略可) ---------------------------------------
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

    // -- 接続情報作成 ---------------------------------------------------
    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             creds->host, creds->port, creds->dbname, creds->user, creds->password);

    // -- PostgreSQL 接続 ------------------------------------------------
    PGconn *conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }
    printf("Successfully connected to PostgreSQL!\n");

    // -- 同じクエリをテキストモード & バイナリモードで実行 -------------
    const char *query = "SELECT * FROM population_00000 WHERE mesh_id=392777384 ORDER BY datetime LIMIT 10"; // 適宜変更

    PGresult *res_text = PQexecParams(
        conn,
        query,
        0,      // パラメータ数
        NULL,   // paramTypes
        NULL,   // paramValues
        NULL,   // paramLengths
        NULL,   // paramFormats
        0       // resultFormat=0 => テキストモード
    );

    if (PQresultStatus(res_text) != PGRES_TUPLES_OK) {
        fprintf(stderr, "[Text mode] Query failed: %s\n", PQerrorMessage(conn));
        PQclear(res_text);
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }

    PGresult *res_bin = PQexecParams(
        conn,
        query,
        0, NULL, NULL, NULL, NULL,
        1       // resultFormat=1 => バイナリモード
    );
    if (PQresultStatus(res_bin) != PGRES_TUPLES_OK) {
        fprintf(stderr, "[Binary mode] Query failed: %s\n", PQerrorMessage(conn));
        PQclear(res_text);
        PQclear(res_bin);
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }

    // -- 行数・列数の比較 -----------------------------------------------
    int nrows_text = PQntuples(res_text);
    int nrows_bin  = PQntuples(res_bin);
    int nfields_text = PQnfields(res_text);
    int nfields_bin  = PQnfields(res_bin);

    if (nrows_text != nrows_bin || nfields_text != nfields_bin) {
        fprintf(stderr, "Mismatch in row/field count: text(%d rows, %d cols), bin(%d rows, %d cols)\n",
                nrows_text, nfields_text, nrows_bin, nfields_bin);
        // 必要に応じてテスト失敗扱い
    }

    // -- カラムインデックスを探す (mesh_id, datetime) -----------------
    int idx_mesh_text = -1, idx_dt_text = -1;
    int idx_mesh_bin  = -1, idx_dt_bin  = -1;
    for (int i = 0; i < nfields_text; i++) {
        const char *colname = PQfname(res_text, i);
        if (strcmp(colname, "mesh_id") == 0) {
            idx_mesh_text = i;
        } else if (strcmp(colname, "datetime") == 0) {
            idx_dt_text = i;
        }
    }
    for (int i = 0; i < nfields_bin; i++) {
        const char *colname = PQfname(res_bin, i);
        if (strcmp(colname, "mesh_id") == 0) {
            idx_mesh_bin = i;
        } else if (strcmp(colname, "datetime") == 0) {
            idx_dt_bin = i;
        }
    }
    if (idx_mesh_text < 0 || idx_dt_text < 0 || idx_mesh_bin < 0 || idx_dt_bin < 0) {
        fprintf(stderr, "Failed to find 'mesh_id' or 'datetime' column\n");
        PQclear(res_text);
        PQclear(res_bin);
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }

    // -- テキストモードとバイナリモードの差異を比較 --------------------
    int mismatch_count = 0;
    int rows_to_compare = (nrows_text < nrows_bin) ? nrows_text : nrows_bin; // 同じはずだが安全のため

    for (int r = 0; r < rows_to_compare; r++) {
        // テキストモード: mesh_id (文字列)->atoi, datetime(文字列)->time_t
        if (PQgetisnull(res_text, r, idx_mesh_text) || PQgetisnull(res_text, r, idx_dt_text)) {
            fprintf(stderr, "Text mode row=%d has NULL\n", r);
            mismatch_count++;
            continue;
        }
        const char *mesh_str_text = PQgetvalue(res_text, r, idx_mesh_text);
        const char *dt_str_text   = PQgetvalue(res_text, r, idx_dt_text);

        int mesh_text_val = atoi(mesh_str_text);
        time_t dt_text_val = parse_text_timestamp(dt_str_text);

        // バイナリモード: mesh_id(4バイト), datetime(8バイト)
        if (PQgetisnull(res_bin, r, idx_mesh_bin) || PQgetisnull(res_bin, r, idx_dt_bin)) {
            fprintf(stderr, "Binary mode row=%d has NULL\n", r);
            mismatch_count++;
            continue;
        }
        const char *mesh_bin_ptr = PQgetvalue(res_bin, r, idx_mesh_bin);
        const char *dt_bin_ptr   = PQgetvalue(res_bin, r, idx_dt_bin);

        // mesh_id: 4バイト big-endian -> ntohl
        if (PQgetlength(res_bin, r, idx_mesh_bin) < 4) {
            fprintf(stderr, "Binary mode row=%d mesh_id length < 4\n", r);
            mismatch_count++;
            continue;
        }
        int32_t mesh_id_net = 0;
        memcpy(&mesh_id_net, mesh_bin_ptr, 4);
        int mesh_bin_val = (int)ntohl(mesh_id_net);

        // datetime: 8バイト -> 1970エポック秒へ変換
        time_t dt_bin_val = pg_bin_timestamp_to_jst(dt_bin_ptr, PQgetlength(res_bin, r, idx_dt_bin));
        volatile int time_index = get_time_index_mobaku_datetime_from_time(dt_bin_val);

        // -- 比較結果の表示 or 差異チェック ---------------------------
        if (mesh_text_val != mesh_bin_val) {
            fprintf(stderr,
                    "Row=%d mismatch mesh_id: text=%d, bin=%d\n",
                    r, mesh_text_val, mesh_bin_val);
            mismatch_count++;
        }

        if (dt_text_val != dt_bin_val) {
            // 多少のズレを許容したいなら差分の絶対値が何秒以下か見るなど
            fprintf(stderr,
                    "Row=%d mismatch datetime: text_str='%s' => %ld, bin => %ld\n",
                    r, dt_str_text, (long)dt_text_val, (long)dt_bin_val);
            mismatch_count++;
        }
    }

    // -- 結果を表示 ----------------------------------------------------
    if (mismatch_count == 0) {
        printf("Text mode vs Binary mode: all %d rows matched!\n", rows_to_compare);
    } else {
        printf("Found %d mismatches in %d rows.\n", mismatch_count, rows_to_compare);
    }

    // -- 後始末 --------------------------------------------------------
    PQclear(res_text);
    PQclear(res_bin);
    PQfinish(conn);
    free_credentials(creds);

    return 0;
}
