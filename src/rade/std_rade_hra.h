/* Information half of the LDPC(112,56) HRA_56_56 parity check matrix used
 * by the FreeDV RADE V1 end-of-over callsign. Each row is a 56-bit mask,
 * bit j = column j. The parity half is lower-bidiagonal and is not stored.
 *
 * Matrix values from freedv/freedv-backend src/pipeline/HRA_56_56.h,
 * Copyright Mooneer Salem, BSD 2-clause:
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. */

#pragma once

static const unsigned long long std_rade_hra_rows[56] = {
    0x00010000002008ULL,
    0x40200100000000ULL,
    0x00404000200000ULL,
    0x00080004000002ULL,
    0x80040000000040ULL,
    0x00200000200800ULL,
    0x000000000c4000ULL,
    0x40110000000000ULL,
    0x00000000108020ULL,
    0x00008000400002ULL,
    0x00001040000080ULL,
    0x18000000000008ULL,
    0x00000000001005ULL,
    0x000000800000c0ULL,
    0x0c000000080000ULL,
    0x00000000201200ULL,
    0x000000c0000002ULL,
    0x00000000800401ULL,
    0x00080048000000ULL,
    0x00020800000040ULL,
    0x01000000000404ULL,
    0x00002009000000ULL,
    0x00800020020000ULL,
    0x00000000940000ULL,
    0x80002000010000ULL,
    0x00800002004000ULL,
    0x02020000000080ULL,
    0x00004010001000ULL,
    0x00000002400100ULL,
    0x02000100000800ULL,
    0x20104000000000ULL,
    0x00000002040020ULL,
    0x00001008010000ULL,
    0x80008080000000ULL,
    0x00000000002204ULL,
    0x00000004400020ULL,
    0x00210000000200ULL,
    0x00080001020000ULL,
    0x00100010000008ULL,
    0x00040a00000000ULL,
    0x10000010000001ULL,
    0x09000000002000ULL,
    0x0000a000000100ULL,
    0x00000000100410ULL,
    0x04000020004000ULL,
    0x00420000000800ULL,
    0x00000020008010ULL,
    0x02001400000000ULL,
    0x05000000000010ULL,
    0x00040400010000ULL,
    0x00000004028000ULL,
    0x20400800000000ULL,
    0x10000000880000ULL,
    0x00800001000100ULL,
    0x60000200000000ULL,
    0x00000700000000ULL,
};
