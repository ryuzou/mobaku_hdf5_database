//
// Created by ryuzot on 25/01/02.
//

#include <stdio.h>
#include <cmph.h>
#include <string.h>
#include <stdint.h>
#include "meshid_ops.h"


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
uint32_t search_id(cmph_t *hash, const char *key) {
    return cmph_search(hash, key, (cmph_uint32)strlen(key));
}

int main() {
    printf("Mesh ID list size: %zu\n", meshid_list_size);
    for (size_t i = 0; i < meshid_list_size; ++i) {
        printf("Mesh ID[%zu]: %d\n", i, meshid_list[i]);
        if (i >= 100) {
            break; // 最初の100個のメッシュIDのみ表示
        }
    }

    // 検索準備
    cmph_t *hash = prepare_search();
    if (!hash) {
        return 1; // 検索準備に失敗した場合は終了
    }

    // 検索するキーを指定
    char *key = "362257492";
    uint32_t id = search_id(hash, key);

    // 検索結果を確認
    if (id >= meshid_list_size) {
        fprintf(stderr, "ID %d is out of bounds for meshid_list.\n", id);
    } else {
        printf("Found ID: %d\n", id);
        printf("Mesh ID: %d\n", meshid_list[id]);
    }

    cmph_destroy(hash);
    return 0;
}