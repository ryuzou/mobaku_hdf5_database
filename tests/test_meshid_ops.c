//
// Created by ryuzot on 25/01/02.
//

#include <stdio.h>
#include <cmph.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "meshid_ops.h"

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

// 検索準備関数
cmph_t* prepare_search(void) {
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

// 検索関数
uint32_t search_id(cmph_t *hash, uint32_t key) {
    char key_str[11];
    uint2str(key, key_str);
    return cmph_search(hash, key_str, (cmph_uint32)strlen(key_str));
}

int main() {
    printf("Mesh ID list size: %zu\n", meshid_list_size);

    // 最初の100個のメッシュIDを表示
    for (size_t i = 0; i < meshid_list_size && i < 10; ++i) {
        printf("Mesh ID[%zu]: %d\n", i, meshid_list[i]);
    }

    // 検索準備
    cmph_t *hash = prepare_search();
    if (!hash) {
        return 1; // 検索準備に失敗した場合は終了
    }

    // 乱数生成の初期化
    srand((unsigned int)time(NULL));

    // 検索するキーを1万個生成
    const int num_searches = 1000000;
    uint32_t *keys = malloc(num_searches * sizeof(uint32_t));
    for (int i = 0; i < num_searches; i++) {
        keys[i] = meshid_list[rand() % meshid_list_size]; // meshid_list_size内の乱数を生成
    }

    // 検索時間計測
    clock_t start_time = clock();
    for (int i = 0; i < num_searches; i++) {
        uint32_t id = search_id(hash, keys[i]);
        assert(meshid_list[id] == keys[i]); // 検索結果が期待されるメッシュIDと一致することを確認
    }
    clock_t end_time = clock();

    // 結果を表示
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Time taken for %d searches: %f seconds\n", num_searches, time_taken);

    // メモリ解放
    free(keys);
    cmph_destroy(hash);
    return 0;
}