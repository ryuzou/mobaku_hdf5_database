//
// Created by ryuzot on 25/01/02.
//

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
