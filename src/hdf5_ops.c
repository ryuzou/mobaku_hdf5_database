#include "hdf5_ops.h""
#include <stdlib.h>
#include <string.h>

hdf5_thread_safe_t* hdf5_create(const char* filename, const char* dataset_name, hsize_t size) {
    hdf5_thread_safe_t* hdf5 = malloc(sizeof(hdf5_thread_safe_t));
    hdf5->file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    hsize_t dims[1] = { size };
    hid_t dataspace_id = H5Screate_simple(1, dims, NULL);
    hdf5->dataset_id = H5Dcreate(hdf5->file_id, dataset_name, H5T_NATIVE_INT, dataspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    pthread_mutex_init(&hdf5->mutex, NULL);
    H5Sclose(dataspace_id);
    return hdf5;
}

void hdf5_write(hdf5_thread_safe_t* hdf5, const void* data, hsize_t offset, hsize_t count) {
    pthread_mutex_lock(&hdf5->mutex);

    hsize_t mem_size[1] = { count };
    hid_t mem_space = H5Screate_simple(1, mem_size, NULL);
    hid_t file_space = H5Dget_space(hdf5->dataset_id);
    H5Sselect_hyperslab(file_space, H5S_SELECT_SET, &offset, NULL, &count, NULL);

    H5Dwrite(hdf5->dataset_id, H5T_NATIVE_INT, mem_space, file_space, H5P_DEFAULT, data);

    H5Sclose(mem_space);
    H5Sclose(file_space);
    pthread_mutex_unlock(&hdf5->mutex);
}

void hdf5_close(hdf5_thread_safe_t* hdf5) {
    H5Dclose(hdf5->dataset_id);
    H5Fclose(hdf5->file_id);
    pthread_mutex_destroy(&hdf5->mutex);
    free(hdf5);
}