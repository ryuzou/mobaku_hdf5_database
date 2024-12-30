#include "hdf5_ops.h"
#include <stdio.h>
#include <stdlib.h>

int write_3d_float_dataset(const char *filename, const char *dataset_name,
                           int dim0, int dim1, int dim2) {
    // 1. ファイルを新規作成
    hid_t file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file_id < 0) {
        fprintf(stderr, "Error: Failed to create file %s\n", filename);
        return -1;
    }

    // 2. データスペース
    hsize_t dims[3] = {(hsize_t) dim0, (hsize_t) dim1, (hsize_t) dim2};
    hid_t space_id = H5Screate_simple(3, dims, NULL);
    if (space_id < 0) {
        fprintf(stderr, "Error: Failed to create dataspace.\n");
        H5Fclose(file_id);
        return -1;
    }

    // 3. 連続レイアウトを指定
    hid_t dcpl_id = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_layout(dcpl_id, H5D_CONTIGUOUS);

    hid_t dset_id = H5Dcreate2(file_id, dataset_name, H5T_NATIVE_UINT32_g,
                               space_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT);
    if (dset_id < 0) {
        fprintf(stderr, "Error: Failed to create dataset.\n");
        H5Sclose(space_id);
        H5Fclose(file_id);
        return -1;
    }

    // 5. 書き込むデータを確保
    uint32_t *data = (uint32_t *) malloc(sizeof(uint32_t) * dim0 * dim1 * dim2);
    if (!data) {
        fprintf(stderr, "Memory allocation failed.\n");
        H5Dclose(dset_id);
        H5Sclose(space_id);
        H5Fclose(file_id);
        return -1;
    }

    // 適当に値をセット (例: t + y + x)
    for (int t = 0; t < dim0; t++) {
        for (int j = 0; j < dim1; j++) {
            for (int i = 0; i < dim2; i++) {
                data[t * (dim1 * dim2) + j * dim2 + i] = (uint32_t) (t + j + i);
            }
        }
    }

    // 6. 書き込み
    herr_t status = H5Dwrite(dset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL,
                             H5P_DEFAULT, data);
    if (status < 0) {
        fprintf(stderr, "Error: Failed to write data.\n");
    }

    // 7. 後始末
    free(data);
    H5Dclose(dset_id);
    H5Sclose(space_id);
    H5Pclose(dcpl_id);
    H5Fclose(file_id);

    return (status < 0) ? -1 : 0;
}

int read_single_float_value(const char *filename, const char *dataset_name,
                            int t, int y, int x, float *out_value) {
    // 1. ファイルをオープン (読み取り専用)
    hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) {
        fprintf(stderr, "Error: Failed to open file %s\n", filename);
        return -1;
    }

    // 2. データセットをオープン
    hid_t dset_id = H5Dopen2(file_id, dataset_name, H5P_DEFAULT);
    if (dset_id < 0) {
        fprintf(stderr, "Error: Failed to open dataset %s\n", dataset_name);
        H5Fclose(file_id);
        return -1;
    }

    // 3. データスペースから次元を取得（ここでは一括読み向けに使う）
    hid_t space_id = H5Dget_space(dset_id);
    hsize_t dims[3];
    int ndims = H5Sget_simple_extent_dims(space_id, dims, NULL);
    if (ndims != 3) {
        fprintf(stderr, "Error: Dataset is not 3D.\n");
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Fclose(file_id);
        return -1;
    }

    // 範囲チェック (簡易)
    if (t < 0 || t >= (int) dims[0] ||
        y < 0 || y >= (int) dims[1] ||
        x < 0 || x >= (int) dims[2]) {
        fprintf(stderr, "Error: Requested index out of range.\n");
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Fclose(file_id);
        return -1;
    }

    // 4. 全体を一括読み(例: 本当はハイパースラブでもOK)
    float *buf = (float *) malloc(sizeof(float) * dims[0] * dims[1] * dims[2]);
    if (!buf) {
        fprintf(stderr, "Memory allocation failed.\n");
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Fclose(file_id);
        return -1;
    }

    herr_t status = H5Dread(dset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL,
                            H5P_DEFAULT, buf);
    if (status < 0) {
        fprintf(stderr, "Error: H5Dread failed.\n");
        free(buf);
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Fclose(file_id);
        return -1;
    }

    // 5. 要素を取得
    *out_value = buf[t * (dims[1] * dims[2]) + y * (dims[2]) + x];
    free(buf);

    // 6. 後始末
    H5Sclose(space_id);
    H5Dclose(dset_id);
    H5Fclose(file_id);

    return 0;
}
