#include <stdio.h>
#include <stdlib.h>
#include "hdf5_ops.h"  // ライブラリのヘッダ

#define TEST_FILE       "test_example.h5"
#define TEST_DATASET    "test_dataset"
#define D0  5
#define D1  50
#define D2  60

int main(void)
{
    // 1. データセットを書き込むテスト
    int ret = write_3d_float_dataset(TEST_FILE, TEST_DATASET, D0, D1, D2);
    if (ret < 0) {
        fprintf(stderr, "write_3d_float_dataset() failed.\n");
        return 1;
    }
    printf("Created and wrote dataset: %s in %s\n", TEST_DATASET, TEST_FILE);

    // 2. 単一要素を読み込むテスト
    float val = 0.0f;
    ret = read_single_float_value(TEST_FILE, TEST_DATASET, 2, 10, 20, &val);
    if (ret < 0) {
        fprintf(stderr, "read_single_float_value() failed.\n");
        return 1;
    }
    printf("Read [2,10,20] = %f (expected: 2+10+20=32.0)\n", val);

    // 3. 簡易判定
    if (val == 32.0f) {
        printf("Test passed!\n");
    } else {
        printf("Test mismatch! expected=32.0\n");
        return 1;
    }

    return 0;
}
