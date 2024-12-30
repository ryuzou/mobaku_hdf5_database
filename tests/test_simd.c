//
// Created by ryuzot on 24/12/23.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <time.h>

// アライメントされたメモリ確保
double* allocate_aligned_memory(size_t size) {
    void* ptr;
    if (posix_memalign(&ptr, 64, size * sizeof(double))) {
        return NULL;
    }
    return (double*)ptr;
}

// 通常のメモリ確保（アライメントなし）
double* allocate_unaligned_memory(size_t size) {
    return (double*)malloc(size * sizeof(double));
}

// 通常のメモリコピー
void normal_copy(double* dst, const double* src, size_t size) {
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

// アライメントなしのメモリコピー
void unaligned_copy(double* dst, const double* src, size_t size) {
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

// AVX2を使用したメモリコピー
void avx2_copy(double* dst, const double* src, size_t size) {
    size_t i;
    size_t aligned_size = size - (size % 4);

    for (i = 0; i < aligned_size; i += 4) {
        __m256d v = _mm256_load_pd(&src[i]);
        _mm256_store_pd(&dst[i], v);
    }

    for (; i < size; i++) {
        dst[i] = src[i];
    }
}

// AVX512を使用したメモリコピー
void avx512_copy(double* dst, const double* src, size_t size) {
    size_t i;
    size_t aligned_size = size - (size % 8);

    for (i = 0; i < aligned_size; i += 8) {
        __m512d v = _mm512_load_pd(&src[i]);
        _mm512_store_pd(&dst[i], v);
    }

    for (; i < size; i++) {
        dst[i] = src[i];
    }
}

// 実行時間計測用の関数
double measure_time(void (*func)(double*, const double*, size_t),
                   double* dst, const double* src, size_t size) {
    clock_t start = clock();
    func(dst, src, size);
    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main() {
    const size_t sizes[] = {1000000, 5000000, 10000000, 50000000};
    const int num_tests = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_tests; i++) {
        size_t size = sizes[i];
        printf("\ntest size: %zu\n", size);

        double* src = allocate_aligned_memory(size);
        double* dst1 = allocate_aligned_memory(size);
        double* dst2 = allocate_aligned_memory(size);
        double* dst3 = allocate_unaligned_memory(size);
        double* dst4 = allocate_aligned_memory(size);
        double* dst5 = allocate_aligned_memory(size);

        if (!src || !dst1 || !dst2 || !dst3 || !dst4 || !dst5) {
            printf("メモリ確保エラー\n");
            return 1;
        }

        for (size_t j = 0; j < size; j++) {
            src[j] = (double)j;
        }

        double normal_time = measure_time(normal_copy, dst1, src, size);
        double unaligned_time = measure_time(unaligned_copy, dst3, src, size);
        double avx2_time = measure_time(avx2_copy, dst2, src, size);
        double avx512_time = measure_time(avx512_copy, dst4, src, size);

        int is_correct = 1;
        for (size_t j = 0; j < size; j++) {
            if (dst1[j] != dst2[j] || dst1[j] != dst3[j] || dst1[j] != dst4[j]) {
                is_correct = 0;
                break;
            }
        }

        printf("normal with aligment time: %f s\n", normal_time);
        printf("normal without alignment time: %f s\n", unaligned_time);
        printf("AVX2 time: %f s\n", avx2_time);
        printf("AVX512 time: %f s\n", avx512_time);
        printf("ratio (AVX512/Normal): %.2fx\n", normal_time / avx512_time);
        printf("validation: %s\n", is_correct ? "OK" : "Error");

        free(src);
        free(dst1);
        free(dst2);
        free(dst3);
        free(dst4);
        free(dst5);
    }

    return 0;
}
