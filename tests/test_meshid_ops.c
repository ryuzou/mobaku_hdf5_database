//
// Created by ryuzot on 25/01/02.
//

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "meshid_ops.h"

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
    uint32_t *keys = malloc(meshid_list_size * sizeof(uint32_t));
    for (int i = 0; i < meshid_list_size; i++) {
        keys[i] = meshid_list[i]; // meshid_list_size内の乱数を生成
    }
    printf("Total length of meshid: %lu\n",meshid_list_size);

    // 検索時間計測
    clock_t start_time = clock();
    for (int i = 0; i < meshid_list_size; i++) {
        uint32_t id = search_id(hash, keys[i]);
        assert(meshid_list[id] == keys[i]); // 検索結果が期待されるメッシュIDと一致することを確認
    }
    clock_t end_time = clock();

    // 結果を表示
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Time taken for %lu searches: %f seconds\n", meshid_list_size, time_taken);

    // メモリ解放
    free(keys);
    cmph_destroy(hash);
    return 0;
}