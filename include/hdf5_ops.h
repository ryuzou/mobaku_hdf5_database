#ifndef HDF5_OPS_H
#define HDF5_OPS_H

#include <hdf5.h>

// 例として、以下のような関数インターフェースを宣言
#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Create and write a 3D float dataset in a new HDF5 file (Contiguous).
     * @param filename  Path to the HDF5 file to create.
     * @param dataset_name  Name of the dataset to create.
     * @param dim0, dim1, dim2 Dimensions of the dataset
     * @return 0 on success, negative on error
     */
    int write_3d_float_dataset(const char* filename, const char* dataset_name,
                               int dim0, int dim1, int dim2);

    /**
     * @brief Read a single element (t, y, x) from a 3D float dataset in HDF5.
     * @param filename     Path to the existing HDF5 file
     * @param dataset_name Name of the dataset
     * @param t, y, x      Coordinates to read
     * @param out_value    Pointer to float, result is stored here
     * @return 0 on success, negative on error
     */
    int read_single_float_value(const char* filename, const char* dataset_name,
                                int t, int y, int x, float* out_value);

#ifdef __cplusplus
}
#endif

#endif // HDF5_OPS_H
