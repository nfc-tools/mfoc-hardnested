//-----------------------------------------------------------------------------
// Copyright (C) 2016, 2017 by piwi
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.ch b
//-----------------------------------------------------------------------------
// Implements a card only attack based on crypto text (encrypted nonces
// received during a nested authentication) only. Unlike other card only
// attacks this doesn't rely on implementation errors but only on the
// inherent weaknesses of the crypto1 cypher. Described in
//   Carlo Meijer, Roel Verdult, "Ciphertext-only Cryptanalysis on Hardened
//   Mifare Classic Cards" in Proceedings of the 22nd ACM SIGSAC Conference on
//   Computer and Communications Security, 2015
//-----------------------------------------------------------------------------
// some helper functions which can benefit from SIMD instructions or other special instructions
//

#include <stdint.h>
#include <stdlib.h>

uint32_t* malloc_bitarray_AVX(uint32_t x) {
    return __builtin_assume_aligned(_aligned_malloc((x), 16), 16);
}

void free_bitarray_AVX(uint32_t* x) {
    _aligned_free(x);
}


void bitarray_AND_AVX(uint32_t* restrict A, uint32_t* restrict B) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    for (uint32_t i = 0; i < (1 << 19); i++) {
        A[i] &= B[i];
    }
}

uint32_t count_bitarray_AND_AVX(uint32_t* restrict A, uint32_t* restrict B) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    uint32_t count = 0;
    for (uint32_t i = 0; i < (1 << 19); i++) {
        A[i] &= B[i];
        count += __builtin_popcountl(A[i]);
    }
    return count;
}

uint32_t count_bitarray_low20_AND_AVX(uint32_t* restrict A, uint32_t* restrict B) {
    uint16_t* a = (uint16_t*)__builtin_assume_aligned(A, 16);
    uint16_t* b = (uint16_t*)__builtin_assume_aligned(B, 16);
    uint32_t count = 0;

    for (uint32_t i = 0; i < (1 << 20); i++) {
        if (!b[i]) {
            a[i] = 0;
        }
        count += __builtin_popcountl(a[i]);
    }
    return count;
}

void bitarray_AND4_AVX(uint32_t* restrict A, uint32_t* restrict B, uint32_t* restrict C, uint32_t* restrict D) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    C = __builtin_assume_aligned(C, 16);
    D = __builtin_assume_aligned(D, 16);
    for (uint32_t i = 0; i < (1 << 19); i++) {
        A[i] = B[i] & C[i] & D[i];
    }
}

void bitarray_OR_AVX(uint32_t* restrict A, uint32_t* restrict B) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    for (uint32_t i = 0; i < (1 << 19); i++) {
        A[i] |= B[i];
    }
}

uint32_t count_bitarray_AND2_AVX(uint32_t* restrict A, uint32_t* restrict B) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    uint32_t count = 0;
    for (uint32_t i = 0; i < (1 << 19); i++) {
        count += __builtin_popcountl(A[i] & B[i]);
    }
    return count;
}

uint32_t count_bitarray_AND3_AVX(uint32_t* restrict A, uint32_t* restrict B, uint32_t* restrict C) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    C = __builtin_assume_aligned(C, 16);
    uint32_t count = 0;
    for (uint32_t i = 0; i < (1 << 19); i++) {
        count += __builtin_popcountl(A[i] & B[i] & C[i]);
    }
    return count;
}

uint32_t count_bitarray_AND4_AVX(uint32_t* restrict A, uint32_t* restrict B, uint32_t* restrict C, uint32_t* restrict D) {
    A = __builtin_assume_aligned(A, 16);
    B = __builtin_assume_aligned(B, 16);
    C = __builtin_assume_aligned(C, 16);
    D = __builtin_assume_aligned(D, 16);
    uint32_t count = 0;
    for (uint32_t i = 0; i < (1 << 19); i++) {
        count += __builtin_popcountl(A[i] & B[i] & C[i] & D[i]);
    }
    return count;
}

