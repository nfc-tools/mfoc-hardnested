//-----------------------------------------------------------------------------
// Copyright (C) 2016, 2017 by piwi
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Implements a card only attack based on crypto text (encrypted nonces
// received during a nested authentication) only. Unlike other card only
// attacks this doesn't rely on implementation errors but only on the
// inherent weaknesses of the crypto1 cypher. Described in
//   Carlo Meijer, Roel Verdult, "Ciphertext-only Cryptanalysis on Hardened
//   Mifare Classic Cards" in Proceedings of the 22nd ACM SIGSAC Conference on 
//   Computer and Communications Security, 2015
//-----------------------------------------------------------------------------
//
// brute forcing is based on @aczids bitsliced brute forcer
// https://github.com/aczid/crypto1_bs with some modifications. Mainly:
// - don't rollback. Start with 2nd byte of nonce instead
// - reuse results of filter subfunctions
// - reuse results of previous nonces if some first bits are identical
// 
//-----------------------------------------------------------------------------
// aczid's Copyright notice:
//
// Bit-sliced Crypto-1 brute-forcing implementation
// Builds on the data structures returned by CraptEV1 craptev1_get_space(nonces, threshold, uid)
/*
Copyright (c) 2015-2016 Aram Verstegen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
 */

#include "hardnested_bruteforce.h"
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include "hardnested_cpu_dispatch.h"
#include "../ui.h"
#include "../util.h"
#include "../util_posix.h"
#include "../crapto1.h"
#include "../cmdhfmfhard.h"
#include "../parity.h"
#include "../mifare.h"
#include "../bf_bench_data.h"

#define DEFAULT_BRUTE_FORCE_RATE  (120000000.0)  // if benchmark doesn't succeed
#define TEST_BENCH_SIZE     (6000)    // number of odd and even states for brute force benchmark


static uint32_t nonces_to_bruteforce = 0;
static uint32_t bf_test_nonce[256];
static uint8_t bf_test_nonce_2nd_byte[256];
static uint8_t bf_test_nonce_par[256];
static uint32_t bucket_count = 0;
static statelist_t* buckets[128];
static uint32_t keys_found = 0;
static uint64_t num_keys_tested;

uint8_t trailing_zeros(uint8_t byte) {
    static const uint8_t trailing_zeros_LUT[256] = {
        8, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
        4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
    };

    return trailing_zeros_LUT[byte];
}

bool verify_key(uint32_t cuid, noncelist_t *nonces, uint8_t *best_first_bytes, uint32_t odd, uint32_t even) {
    struct Crypto1State pcs;
    for (uint16_t test_first_byte = 1; test_first_byte < 256; test_first_byte++) {
        noncelistentry_t *test_nonce = nonces[best_first_bytes[test_first_byte]].first;
        while (test_nonce != NULL) {
            pcs.odd = odd;
            pcs.even = even;
            lfsr_rollback_byte(&pcs, (cuid >> 24) ^ best_first_bytes[0], true);
            for (int8_t byte_pos = 3; byte_pos >= 0; byte_pos--) {
                uint8_t test_par_enc_bit = (test_nonce->par_enc >> byte_pos) & 0x01; // the encoded parity bit
                uint8_t test_byte_enc = (test_nonce->nonce_enc >> (8 * byte_pos)) & 0xff; // the encoded nonce byte
                uint8_t test_byte_dec = crypto1_byte(&pcs, test_byte_enc /* ^ (cuid >> (8*byte_pos)) */, true) ^ test_byte_enc; // decode the nonce byte	
                uint8_t ks_par = filter(pcs.odd); // the keystream bit to encode/decode the parity bit
                uint8_t test_par_enc2 = ks_par ^ evenparity8(test_byte_dec); // determine the decoded byte's parity and encode it
                if (test_par_enc_bit != test_par_enc2) {
                    return false;
                }
            }
            test_nonce = test_nonce->next;
        }
    }
    return true;
}

static void*
#ifdef __has_attribute
#if __has_attribute(force_align_arg_pointer)
__attribute__((force_align_arg_pointer))
#endif
#endif
crack_states_thread(void* x) {

    struct arg {
        bool silent;
        int thread_ID;
        uint32_t cuid;
        uint32_t num_acquired_nonces;
        uint64_t maximum_states;
        noncelist_t *nonces;
        uint8_t* best_first_bytes;
        uint8_t trgBlock;
        uint8_t trgKey;
    } *thread_arg;

    thread_arg = (struct arg *) x;
    const int thread_id = thread_arg->thread_ID;
    uint32_t current_bucket = thread_id;
    while (current_bucket < bucket_count) {
        statelist_t *bucket = buckets[current_bucket];
        if (bucket) {
            const uint64_t key = crack_states_bitsliced(thread_arg->cuid, thread_arg->best_first_bytes, bucket, &keys_found, &num_keys_tested, nonces_to_bruteforce, bf_test_nonce_2nd_byte, thread_arg->nonces);
            if (key != -1) {
                __sync_fetch_and_add(&keys_found, 1);
                char progress_text[80];
                sprintf(progress_text, "brute force phase completed");
                if (thread_arg->trgKey == MC_AUTH_A){
                    t.sectors[block_to_sector(thread_arg->trgBlock)].foundKeyA = true;
                    num_to_bytes(key, 6, t.sectors[block_to_sector(thread_arg->trgBlock)].KeyA);
                } else {
                    t.sectors[block_to_sector(thread_arg->trgBlock)].foundKeyB = true;
                    num_to_bytes(key, 6, t.sectors[block_to_sector(thread_arg->trgBlock)].KeyB);
                }
                hardnested_print_progress(progress_text);
                break;
            } else if (keys_found) {
                break;
            } else {
                if (!thread_arg->silent) {
                    char progress_text[80];
                    sprintf(progress_text, "brute force phase: %6.02f%%", 100.0 * (float) num_keys_tested / (float) (thread_arg->maximum_states));
                    float remaining_bruteforce = thread_arg->nonces[thread_arg->best_first_bytes[0]].expected_num_brute_force - (float) num_keys_tested / 2;
                    hardnested_print_progress(progress_text);
                }
            }
        }
        current_bucket += num_CPUs();
    }
    return NULL;
}

void prepare_bf_test_nonces(noncelist_t *nonces, uint8_t best_first_byte) {
    // we do bitsliced brute forcing with best_first_bytes[0] only.
    // Extract the corresponding 2nd bytes
    noncelistentry_t *test_nonce = nonces[best_first_byte].first;
    uint32_t i = 0;
    while (test_nonce != NULL) {
        bf_test_nonce[i] = test_nonce->nonce_enc;
        bf_test_nonce_par[i] = test_nonce->par_enc;
        bf_test_nonce_2nd_byte[i] = (test_nonce->nonce_enc >> 16) & 0xff;
        test_nonce = test_nonce->next;
        i++;
    }
    nonces_to_bruteforce = i;

    uint8_t best_4[4] = {0};
    int sum_best = -1;
    for (uint16_t n1 = 0; n1 < nonces_to_bruteforce; n1++) {
        for (uint16_t n2 = 0; n2 < nonces_to_bruteforce; n2++) {
            if (n2 != n1) {
                for (uint16_t n3 = 0; n3 < nonces_to_bruteforce; n3++) {
                    if ((n3 != n2 && n3 != n1) || nonces_to_bruteforce < 3
                            ) {
                        for (uint16_t n4 = 0; n4 < nonces_to_bruteforce; n4++) {
                            if ((n4 != n3 && n4 != n2 && n4 != n1) || nonces_to_bruteforce < 4
                                    ) {
                                int sum = nonces_to_bruteforce > 1 ? trailing_zeros(bf_test_nonce_2nd_byte[n1] ^ bf_test_nonce_2nd_byte[n2]) : 0.0
                                        + nonces_to_bruteforce > 2 ? trailing_zeros(bf_test_nonce_2nd_byte[n2] ^ bf_test_nonce_2nd_byte[n3]) : 0.0
                                        + nonces_to_bruteforce > 3 ? trailing_zeros(bf_test_nonce_2nd_byte[n3] ^ bf_test_nonce_2nd_byte[n4]) : 0.0;
                                if (sum > sum_best) {
                                    sum_best = sum;
                                    best_4[0] = n1;
                                    best_4[1] = n2;
                                    best_4[2] = n3;
                                    best_4[3] = n4;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    uint32_t bf_test_nonce_temp[4];
    uint8_t bf_test_nonce_par_temp[4];
    uint8_t bf_test_nonce_2nd_byte_temp[4];
    for (uint8_t i = 0; i < 4 && i < nonces_to_bruteforce; i++) {
        bf_test_nonce_temp[i] = bf_test_nonce[best_4[i]];

        bf_test_nonce_par_temp[i] = bf_test_nonce_par[best_4[i]];
        bf_test_nonce_2nd_byte_temp[i] = bf_test_nonce_2nd_byte[best_4[i]];
    }
    for (uint8_t i = 0; i < 4 && i < nonces_to_bruteforce; i++) {
        bf_test_nonce[i] = bf_test_nonce_temp[i];
        bf_test_nonce_par[i] = bf_test_nonce_par_temp[i];
        bf_test_nonce_2nd_byte[i] = bf_test_nonce_2nd_byte_temp[i];
    }
}

bool brute_force_bs(float *bf_rate, statelist_t *candidates, uint32_t cuid, uint32_t num_acquired_nonces, uint64_t maximum_states, noncelist_t *nonces, uint8_t *best_first_bytes, uint8_t trgBlock, uint8_t trgKey) {
    bool silent = (bf_rate != NULL);
    keys_found = 0;
    num_keys_tested = 0;

    bitslice_test_nonces(nonces_to_bruteforce, bf_test_nonce, bf_test_nonce_par);

    // count number of states to go
    bucket_count = 0;
    for (statelist_t *p = candidates; p != NULL; p = p->next) {
        if (p->states[ODD_STATE] != NULL && p->states[EVEN_STATE] != NULL) {
            buckets[bucket_count] = p;
            bucket_count++;
        }
    }

    uint8_t num_core = num_CPUs();
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * num_core);
    uint64_t start_time = msclock();

    struct args {
        bool silent;
        int thread_ID;
        uint32_t cuid;
        uint32_t num_acquired_nonces;
        uint64_t maximum_states;
        noncelist_t *nonces;
        uint8_t *best_first_bytes;
        uint8_t trgBlock;
        uint8_t trgKey;
    } *thread_args = malloc(num_core * sizeof(*thread_args));

    for (uint8_t i = 0; i < num_core; i++) {
        thread_args[i].thread_ID = i;
        thread_args[i].silent = silent;
        thread_args[i].cuid = cuid;
        thread_args[i].num_acquired_nonces = num_acquired_nonces;
        thread_args[i].maximum_states = maximum_states;
        thread_args[i].nonces = nonces;
        thread_args[i].best_first_bytes = best_first_bytes;
        thread_args[i].trgBlock = trgBlock;
        thread_args[i].trgKey = trgKey;
        pthread_create(&threads[i], NULL, crack_states_thread, (void*) &thread_args[i]);
    }
    for (uint8_t i = 0; i < num_core; i++) {
        pthread_join(threads[i], 0);
    }
    free(thread_args);
    free(threads);
    uint64_t elapsed_time = msclock() - start_time;

    if (bf_rate != NULL) {
        *bf_rate = (float) num_keys_tested / ((float) elapsed_time / 1000.0);
    }

    return (keys_found != 0);
}

static void _read(void *buf, size_t size, size_t count, uint8_t *stream, size_t *pos) {
    size_t len = size * count;
    memcpy(buf, &stream[*pos], len);
    *pos += len;
}

static bool read_bench_data(statelist_t *test_candidates) {
    uint8_t *bench_data = bf_bench_data_bin;
    size_t pos = 0;

    uint32_t temp = 0;
    uint32_t num_states = 0;
    uint32_t states_read = 0;

    _read(&nonces_to_bruteforce, 1, sizeof (nonces_to_bruteforce), bench_data, &pos);
    for (uint16_t i = 0; i < nonces_to_bruteforce && i < 256; i++) {
        _read(&bf_test_nonce[i], 1, sizeof (uint32_t), bench_data, &pos);
        bf_test_nonce_2nd_byte[i] = (bf_test_nonce[i] >> 16) & 0xff;
        _read(&bf_test_nonce_par[i], 1, sizeof (uint8_t), bench_data, &pos);
    }
    _read(&num_states, 1, sizeof (uint32_t), bench_data, &pos);
    for (states_read = 0; states_read < MIN(num_states, TEST_BENCH_SIZE); states_read++) {
        _read(test_candidates->states[EVEN_STATE] + states_read, 1, sizeof (uint32_t), bench_data, &pos);
    }
    for (uint32_t i = states_read; i < TEST_BENCH_SIZE; i++) {
        test_candidates->states[EVEN_STATE][i] = test_candidates->states[EVEN_STATE][i - states_read];
    }
    for (uint32_t i = states_read; i < num_states; i++) {
        _read(&temp, 1, sizeof (uint32_t), bench_data, &pos);
    }
    for (states_read = 0; states_read < MIN(num_states, TEST_BENCH_SIZE); states_read++) {
        _read(test_candidates->states[ODD_STATE] + states_read, 1, sizeof (uint32_t), bench_data, &pos);
    }
    for (uint32_t i = states_read; i < TEST_BENCH_SIZE; i++) {
        test_candidates->states[ODD_STATE][i] = test_candidates->states[ODD_STATE][i - states_read];
    }

    return true;
}

float brute_force_benchmark() {
    uint8_t num_core = num_CPUs();
    statelist_t* test_candidates = malloc(num_core * sizeof(*test_candidates));

    test_candidates[0].states[ODD_STATE] = malloc((TEST_BENCH_SIZE + 1) * sizeof (uint32_t));
    test_candidates[0].states[EVEN_STATE] = malloc((TEST_BENCH_SIZE + 1) * sizeof (uint32_t));
    for (uint8_t i = 0; i < num_core - 1; i++) {
        test_candidates[i].next = test_candidates + i + 1;
        test_candidates[i + 1].states[ODD_STATE] = test_candidates[0].states[ODD_STATE];
        test_candidates[i + 1].states[EVEN_STATE] = test_candidates[0].states[EVEN_STATE];
    }
    test_candidates[num_core - 1].next = NULL;

    if (!read_bench_data(test_candidates)) {
        return DEFAULT_BRUTE_FORCE_RATE;
    }

    for (uint8_t i = 0; i < num_core; i++) {
        test_candidates[i].len[ODD_STATE] = TEST_BENCH_SIZE;
        test_candidates[i].len[EVEN_STATE] = TEST_BENCH_SIZE;
        test_candidates[i].states[ODD_STATE][TEST_BENCH_SIZE] = -1;
        test_candidates[i].states[EVEN_STATE][TEST_BENCH_SIZE] = -1;
    }

    uint64_t maximum_states = TEST_BENCH_SIZE * TEST_BENCH_SIZE * (uint64_t) num_core;

    float bf_rate;
    brute_force_bs(&bf_rate, test_candidates, 0, 0, maximum_states, NULL, 0, 0, 0);

    free(test_candidates[0].states[ODD_STATE]);
    free(test_candidates[0].states[EVEN_STATE]);
    free(test_candidates);
    return bf_rate;
}


