cmake_minimum_required(VERSION 3.30)
project(mobaku_hdf5_server C)

# C標準の設定
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# コンパイラの設定
set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE STRING "clang compiler" FORCE)
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE STRING "clang++ compiler" FORCE)

# ビルドタイプの設定
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3")

find_package(HDF5 REQUIRED COMPONENTS C HL)

# Bellow due to miniconda shit. I hate conda
set(PostgreSQL_HOME /usr/pgsql-16)

if(NOT EXISTS ${PostgreSQL_HOME}/include/libpq-fe.h)
    message(FATAL_ERROR "PostgreSQL header files not found at ${PostgreSQL_HOME}/include")
endif()

if(NOT EXISTS ${PostgreSQL_HOME}/lib/libpq.so)
    message(FATAL_ERROR "PostgreSQL library not found at ${PostgreSQL_HOME}/lib")
endif()

set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_HOME}/include)
set(PostgreSQL_LIBRARIES pq)    # I hate conda

message("PostgreSQL Include Dirs: ${PostgreSQL_INCLUDE_DIRS}")
message("PostgreSQL Libraries: ${PostgreSQL_LIBRARIES}")

add_library(hdf5_lib
        src/hdf5_ops.c
        src/env_reader.c
        src/db_credentials.c
)

target_include_directories(hdf5_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HDF5_INCLUDE_DIRS}
        ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(hdf5_lib PUBLIC
        ${HDF5_LIBRARIES}
        ${PostgreSQL_LIBRARIES}
)

# SIMDオプション (全体に適用)
add_compile_options(-mavx -mavx2)
add_compile_options(-mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl)

# 実行ファイル test_hdf5_ops の作成
add_executable(test_hdf5_ops
        tests/test_hdf5_ops.c
)

target_include_directories(test_hdf5_ops PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

)

target_link_libraries(test_hdf5_ops PUBLIC
        hdf5_lib
)

# 実行ファイル test_simd の作成
add_executable(test_simd
        tests/test_simd.c
)

target_include_directories(test_simd PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

)

target_link_libraries(test_simd PUBLIC
        hdf5_lib
)

add_executable(test_postgres_connection
        tests/test_postgres_connection.c
)

target_include_directories(test_postgres_connection PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_postgres_connection PUBLIC
        hdf5_lib
)