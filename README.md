# Mobaku HDF5 Database

This repository provides a tool for managing and accessing data stored in HDF5 files, conjunction with a PostgreSQL database.

## Key Features

* **HDF5 Data Management:** Creates and manages HDF5 files for storing structured data.
* **Data Chinking by Mesh:** Organizes data into separate HDF5 files based on a "mesh" identifier, improving manageability and potentially parallel processing.
* **Efficient Disk Access:** Utilizes the HDF5 format, which is known for its efficient storage of large, complex datasets and supports features like chunking and compression for optimized disk I/O.

## Prerequisites

A modern Linux system is required for building from source.

* **C23 Compatibility:** A compiler supporting the C23 standard is necessary.
* **External Libraries:** The following libraries must be installed on your system:
    * **hdf5:** For reading and writing HDF5 files.
    * **cmph:**  (Compact Minimal Perfect Hash) Likely used for efficient key lookups, potentially related to the mesh identifiers within the HDF5 files or database.
    * **postgres:** The PostgreSQL client libraries for interacting with a PostgreSQL database.

## Installation (Building from Source)

```shell
mkdir build
cd build
cmake ..
make
```

## Usage

### Configuration

Before running the tools, you need to configure the environment variables.

1. **Copy the example environment file:**
   ```shell
   cp .env.example build/.env
   ```
2. **Edit the `.env` file:** Open the `build/.env` file and fill in the necessary credentials and configurations. The specific variables will depend on your setup but may include database connection details, file paths, or other parameters.

### Using Pre-built Binaries

1. **Download the binaries:** Obtain the pre-built binaries from the releases page: [https://github.com/ryuzou/mobaku_hdf5_database/releases/tag/v1.0.0](https://github.com/ryuzou/mobaku_hdf5_database/releases/tag/v1.0.0)
2. **Place the binaries:** Ensure the downloaded binaries are in a location accessible to you.
3. **Copy the example environment file:**
   ```shell
   cp .env.example .env
   ```
4. **Edit the `.env` file:**  Fill in the credentials in the `.env` file in the same directory as the binaries.

### Examples

#### Creating an HDF5 file for a specific mesh

This command creates an HDF5 file for the mesh identified by `5033`.

```shell
./create_hdf5_for_1st_mesh .env 5033 mesh_5033.h5
```

**Explanation:**

* `./create_hdf5_for_1st_mesh`: This is the executable for creating the HDF5 file.
* `.env`:  Specifies the environment file containing configuration details.
* `5033`: This is the mesh identifier. The tool likely uses this identifier to organize the data within the HDF5 file.
* `mesh_5033.h5`: The name of the HDF5 file to be created.

## License

MIT License