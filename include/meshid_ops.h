//
// Created by ryuzot on 25/01/02.
//

#ifndef MESHID_OPS_H
#define MESHID_OPS_H

#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <cmph.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const size_t meshid_list_size;
extern const uint32_t meshid_list[];
extern unsigned char _binary_meshid_mobaku_mph_start[];
extern unsigned char _binary_meshid_mobaku_mph_end[];
extern unsigned char _binary_meshid_mobaku_mph_size[];

#ifdef __cplusplus
}
#endif

#define REFERENCE_MOBAKU_DATETIME "2016-01-01 00:00:00"

int get_time_index_mobaku_datetime(char *now_time_str);

void uint2str(unsigned int num, char *str);

// 検索準備関数
cmph_t* prepare_search(void);

// 検索関数
uint32_t search_id(cmph_t *hash, uint32_t key);

char** uint_array_to_string_array(const int* int_array, size_t nkeys);

// 文字列配列を解放する関数
void free_string_array(char** str_array, size_t nkeys);

// ハッシュ関数を作成する関数
cmph_t* create_local_mph_from_int(int* int_array, size_t nkeys);

int find_local_id(cmph_t* hash, uint32_t key);

#endif //MESHID_OPS_H
