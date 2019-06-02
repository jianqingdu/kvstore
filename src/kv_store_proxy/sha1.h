//
//  sha1.h
//  kv-store
//
//  Created by ziteng on 17/6/20.
//

#ifndef SHA1_H
#define SHA1_H
/* ================ sha1.h ================ */
/*
 SHA-1 in C
 By Steve Reid <steve@edmweb.com>
 100% Public Domain
 */

#include "util.h"

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

void SHA1Transform(uint32_t state[5], const unsigned char buffer[64]);
void SHA1Init(SHA1_CTX* context);
void SHA1Update(SHA1_CTX* context, const unsigned char* data, uint32_t len);
void SHA1Final(unsigned char digest[20], SHA1_CTX* context);

string SHA1(const string& data);

#endif
