//
// Created by ryuzot on 25/01/02.
//

#include <stdio.h>
#include <cmph.h>

#include "meshid_ops.h"

int main() {
    printf("%d\n", (int)meshid_list_size);
    for (int i = 0; i < meshid_list_size; ++i) {
        printf("%d\n", meshid_list[i]);
        if (i >= 100) {
            break;
        }
    }
    unsigned char *mphf_data = &_binary_meshid_hash_mphf_start;
    size_t mphf_size = (size_t)(&_binary_meshid_hash_mphf_end - &_binary_meshid_hash_mphf_start);

    printf("MPHF data size: %zu bytes\n", mphf_size);

    // (1) もしPOSIXなら fmemopen() で擬似FILE*化して cmph_load()
    FILE *fp = fmemopen(mphf_data, mphf_size, "rb");
    if (!fp) {
        perror("fmemopen");
        return 1;
    }
    cmph_t *hash = cmph_load(fp);
    fclose(fp);

    if(!hash) {
        fprintf(stderr, "Failed to load mphf.\n");
        return 1;
    }

    // ここで cmph_search() 等を行う
    // ...

    cmph_destroy(hash);
    return 0;
}