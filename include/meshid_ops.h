//
// Created by ryuzot on 25/01/02.
//

#ifndef MESHID_OPS_H
#define MESHID_OPS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const size_t meshid_list_size;
extern const uint32_t meshid_list[];
extern unsigned char _binary_meshid_hash_mphf_start;
extern unsigned char _binary_meshid_hash_mphf_end;
extern unsigned char _binary_meshid_hash_mphf_size;

#ifdef __cplusplus
}
#endif

#endif //MESHID_OPS_H
