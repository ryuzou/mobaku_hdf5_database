//
// Created by ryuzot on 25/01/02.
//

#include "meshid_ops.h"

#include <assert.h>

time_t pg_bin_timestamp_to_jst(const char *bin_ptr, int len) {
    if (len < 8) {
        return (time_t)-1;
    }

    int64_t network_order_64 = 0;
    memcpy(&network_order_64, bin_ptr, 8);
    int64_t pg_microsec = (int64_t)be64toh((uint64_t)network_order_64);

    int64_t pg_sec  = pg_microsec / 1000000;
    int64_t utc_sec = pg_sec + POSTGRES_EPOCH_IN_UNIX;
    // AWARE!!! HARD CODED!!!!
    int64_t jst_sec = utc_sec - JST_OFFSET_SEC;

    return (time_t)jst_sec;
}

int get_time_index_mobaku_datetime(char* now_time_str) {
    constexpr struct tm reference_time_tm = {0};
    if (strptime(REFERENCE_MOBAKU_DATETIME, "%Y-%m-%d %H:%M:%S", &reference_time_tm) == NULL) {
        fprintf(stderr, "Failed to parse first datetime_str\n");
        return -1;
    }
    time_t reference_mobaku_time = mktime(&reference_time_tm);

    if (reference_mobaku_time == (time_t)-1) {
        fprintf(stderr, "Failed to create reference time\n");
        return -1;
    }

    struct tm now_time_tm = {0};
    if (strptime(now_time_str, "%Y-%m-%d %H:%M:%S", &now_time_tm) == NULL) {
        fprintf(stderr, "Failed to parse now datetime_str\n");
        return -1;
    }

    time_t now_time = mktime(&now_time_tm);
    if (now_time == (time_t)-1) {
        fprintf(stderr, "Failed to create now time\n");
        return -1;
    }

    int index_h_time = (int)(difftime(now_time, reference_mobaku_time) / 3600.0);
    if (index_h_time < 0) {
        index_h_time = -1;
    }
    return index_h_time;
}

int get_time_index_mobaku_datetime_from_time(time_t now_time) {
    constexpr struct tm reference_time_tm = {0};
    if (strptime(REFERENCE_MOBAKU_DATETIME, "%Y-%m-%d %H:%M:%S", &reference_time_tm) == NULL) {
        fprintf(stderr, "Failed to parse first datetime_str\n");
        return -1;
    }
    time_t reference_mobaku_time = mktime(&reference_time_tm);

    if (reference_mobaku_time == (time_t)-1) {
        fprintf(stderr, "Failed to create reference time\n");
        return -1;
    }

    if (now_time == (time_t)-1) {
        fprintf(stderr, "Failed to create now time\n");
        return -1;
    }

    int index_h_time = (int)(difftime(now_time, reference_mobaku_time) / 3600.0);
    if (index_h_time < 0) {
        index_h_time = -1;
    }
    return index_h_time;
}

char * get_mobaku_datetime_from_time_index(int time_index) {
    struct tm reference_time_tm = {0};
    if (strptime(REFERENCE_MOBAKU_DATETIME, "%Y-%m-%d %H:%M:%S", &reference_time_tm) == NULL) {
        fprintf(stderr, "Failed to parse reference datetime\n");
        return NULL;
    }
    time_t reference_mobaku_time = mktime(&reference_time_tm);

    if (reference_mobaku_time == (time_t)-1) {
        fprintf(stderr, "Failed to create reference time\n");
        return NULL;
    }

    time_t target_time = reference_mobaku_time + (time_index * 3600);

    struct tm target_time_tm;
    localtime_r(&target_time, &target_time_tm);

    char* datetime_str = (char*)malloc(sizeof(char) * 20); // "YYYY-MM-DD HH:MM:SS\0"
    if (datetime_str == NULL) {
        perror("malloc failed");
        return NULL;
    }

    if (strftime(datetime_str, 20, "%Y-%m-%d %H:%M:%S", &target_time_tm) == 0) {
        fprintf(stderr, "Failed to format datetime\n");
        free(datetime_str);
        return NULL;
    }

    return datetime_str;
}

void uint2str(unsigned int num, char *str) {
    int i = 0;

    // 数字を逆順に格納
    do {
        str[i++] = (num % 10) + '0'; // 数字を文字に変換
        num /= 10;
    } while (num > 0);

    // 文字列の終端を追加
    str[i] = '\0';

    // 文字列を逆にする
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}

cmph_t * prepare_search(void) {
    unsigned char *mph_data = _binary_meshid_mobaku_mph_start;
    size_t mph_size = (size_t)(_binary_meshid_mobaku_mph_end - _binary_meshid_mobaku_mph_start);
    FILE *fp = fmemopen(mph_data, mph_size, "rb");
    if (!fp) {
        perror("fmemopen");
        return NULL;
    }

    cmph_t *hash = cmph_load(fp);
    fclose(fp);

    if (!hash) {
        fprintf(stderr, "Failed to load mphf.\n");
    }

    return hash;
}

uint32_t search_id(cmph_t *hash, uint32_t key) {
    if (key == 684827214) {
        return 1553331;
    }
    char key_str[11];
    uint2str(key, key_str);
    return cmph_search(hash, key_str, (cmph_uint32)strlen(key_str));
}

char ** uint_array_to_string_array(const int *int_array, size_t nkeys) {
    char** str_array = (char**)malloc(sizeof(char*) * nkeys);
    if (str_array == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }

    for (size_t i = 0; i < nkeys; ++i) {
        // intの最大桁数を考慮してバッファを確保 (+ ヌル終端)
        str_array[i] = (char*)malloc(sizeof(char) * 12); // intの最大値は10桁+符号+終端
        if (str_array[i] == NULL) {
            perror("Memory allocation failed");
            // 確保済みのメモリを解放
            for (size_t j = 0; j < i; ++j) {
                free(str_array[j]);
            }
            free(str_array);
            return NULL;
        }
        uint2str(int_array[i], str_array[i]);
    }
    return str_array;
}

void free_string_array(char **str_array, size_t nkeys) {
    if (str_array == NULL) return;
    for (size_t i = 0; i < nkeys; ++i) {
        free(str_array[i]);
    }
    free(str_array);
}

cmph_t * create_local_mph_from_int(int *int_array, size_t nkeys) {
    char** str_array = uint_array_to_string_array(int_array, nkeys);
    if (str_array == NULL) return nullptr; // str_array が NULL の場合の処理を追加

    cmph_io_adapter_t* source = cmph_io_vector_adapter(str_array, nkeys);
    cmph_config_t* config = cmph_config_new(source);
    cmph_config_set_algo(config, CMPH_CHM);
    cmph_t* hash = cmph_new(config);
    cmph_config_destroy(config);
    cmph_io_vector_adapter_destroy(source);
    free_string_array(str_array, nkeys); // str_array の解放を追加

    if (hash == nullptr) {
        fprintf(stderr, "Error creating hash function\n");
        return nullptr;
    }

    return hash;
}

int find_local_id(cmph_t *hash, uint32_t key) {
    char key_str[11];
    uint2str(key, key_str);
    return cmph_search(hash, key_str, (cmph_uint32)strlen(key_str));
}

void printProgressBar(int now, int all) {
    const int barWidth = 50;

    double progress = (double)(now) / (double)all;
    int pos = (int)(barWidth * progress);

    // 行頭に戻る(\r)ことで同じ行を上書き
    printf("\r[");
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) {
            printf("=");
        } else {
            printf(" ");
        }
    }
    printf("] %6.2f %%  %d/%d", progress * 100.0, now, all);
    fflush(stdout);
}
