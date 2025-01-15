cmake_minimum_required(VERSION 3.26)
project(mobaku_hdf5_server)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3")

find_package(HDF5 REQUIRED COMPONENTS C HL)

# Bellow due to miniconda shit. I hate conda
set(PostgreSQL_HOME /usr/pgsql-16)
set(LOCAL_INCLUDE /usr/local)

if(NOT EXISTS ${PostgreSQL_HOME}/include/libpq-fe.h)
    message(FATAL_ERROR "PostgreSQL header files not found at ${PostgreSQL_HOME}/include")
endif()

if(NOT EXISTS ${PostgreSQL_HOME}/lib/libpq.so)
    message(FATAL_ERROR "PostgreSQL library not found at ${PostgreSQL_HOME}/lib")
endif()

set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_HOME}/include)
set(PostgreSQL_LIBRARIES pq)    # I hate conda

#cmph
if(NOT EXISTS ${LOCAL_INCLUDE}/include/cmph.h)
    message(FATAL_ERROR "CMPH header files not found at ${LOCAL_INCLUDE}/include")
endif()

if(NOT EXISTS ${LOCAL_INCLUDE}/lib/libcmph.so)
    message(FATAL_ERROR "CMPH library not found at ${LOCAL_INCLUDE}/lib")
endif()

set(CMPH_INCLUDE_DIRS ${LOCAL_INCLUDE}/include)
set(CMPH_LIBRARIES cmph)

SET(OBJS
        ${CMAKE_CURRENT_SOURCE_DIR}/external/meshids/meshid_mobaku.o
        ${CMAKE_CURRENT_SOURCE_DIR}/external/meshids/meshid_mobaku_mph.o
)

add_library(hdf5_lib
        src/hdf5_ops.c
        src/env_reader.c
        src/db_credentials.c
        src/meshid_ops.c
        ${OBJS}
        src/fifioq.c
)

target_include_directories(hdf5_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HDF5_INCLUDE_DIRS}
        ${PostgreSQL_INCLUDE_DIRS}
        ${CMPH_INCLUDE_DIRS}
)

target_link_libraries(hdf5_lib PUBLIC
        ${HDF5_LIBRARIES}
        ${PostgreSQL_LIBRARIES}
        ${CMPH_LIBRARIES}
)

add_compile_options(-mavx -mavx2)
add_compile_options(-mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl)


# CLI Executable
add_executable(create_hdf5_database_from_pg
        src/create_hdf5_database_from_pg.c
)

target_link_libraries(create_hdf5_database_from_pg
        hdf5_lib
)
add_executable(create_hdf5_for_1st_mesh
        src/create_hdf5_for_1st_mesh.c
)

target_link_libraries(create_hdf5_for_1st_mesh
        hdf5_lib
)
# Tests

add_executable(test_hdf5_ops
        tests/test_hdf5_ops.c
)

target_include_directories(test_hdf5_ops PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

)

target_link_libraries(test_hdf5_ops PUBLIC
        hdf5_lib
)

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

add_executable(test_meshid_ops
        tests/test_meshid_ops.c
)

target_include_directories(test_meshid_ops PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_meshid_ops PUBLIC
        hdf5_lib
)

add_executable(test_pg2hdf5Queue
        tests/test_pg2hdf5Queue.c
)

target_link_libraries(test_pg2hdf5Queue PUBLIC
        hdf5_lib
)


SET_SOURCE_FILES_PROPERTIES(
        ${OBJS}
        PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
)