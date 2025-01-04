#ifndef HDF5_OPS_H
#define HDF5_OPS_H

#include "hdf5.h"
#include <pthread.h>

typedef struct {
    hid_t file_id;
    hid_t dataset_id;
    pthread_mutex_t mutex;
} hdf5_thread_safe_t;

// 関数プロトタイプ
hdf5_thread_safe_t* hdf5_create(const char* filename, const char* dataset_name, hsize_t size);
void hdf5_write(hdf5_thread_safe_t* hdf5, const void* data, hsize_t offset, hsize_t count);
void hdf5_close(hdf5_thread_safe_t* hdf5);

#endif // HDF5_OPS_H
