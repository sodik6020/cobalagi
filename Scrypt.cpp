/*
 * Copyright (c) 2014, Airbitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */

#include "Scrypt.hpp"
#include "Random.hpp"
#include "../util/Debug.hpp"
#include "../bitcoin/Testnet.hpp"
#include "../../minilibs/scrypt/crypto_scrypt.h"
#include <sys/time.h>
#include <math.h>

namespace abcd {

#define SCRYPT_DEFAULT_SERVER_N         16384    // can't change as server uses this as well
#define SCRYPT_DEFAULT_SERVER_R         1        // can't change as server uses this as well
#define SCRYPT_DEFAULT_SERVER_P         1        // can't change as server uses this as well
#define SCRYPT_DEFAULT_CLIENT_N_SHIFT   14
#define SCRYPT_DEFAULT_CLIENT_N         (1 << SCRYPT_DEFAULT_CLIENT_N_SHIFT) //16384
#define SCRYPT_DEFAULT_CLIENT_R         1
#define SCRYPT_DEFAULT_CLIENT_P         1
#define SCRYPT_MIN_CLIENT_R             8
#define SCRYPT_MAX_CLIENT_N_SHIFT       17
#define SCRYPT_MAX_CLIENT_N             (1 << SCRYPT_MAX_CLIENT_N_SHIFT)
#define SCRYPT_TARGET_USECONDS          250000

#define SCRYPT_DEFAULT_SALT_LENGTH 32

void
ScryptSnrp::createSnrpFromTime(unsigned long totalTime)
{
    ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime target:%d timing:%d",
                   SCRYPT_TARGET_USECONDS,
                   totalTime);

    double fN = 1.0;
    double fR = (double) SCRYPT_DEFAULT_CLIENT_R;
    double fP = (double) SCRYPT_DEFAULT_CLIENT_P;

    double fEstTargetTimeElapsed = (double) totalTime;
    double maxNShift = 1 + SCRYPT_MAX_CLIENT_N_SHIFT -
                       SCRYPT_DEFAULT_CLIENT_N_SHIFT;

    fR = ((double) SCRYPT_TARGET_USECONDS / fEstTargetTimeElapsed);

    double rRemainder = (double) ((unsigned long) fR % SCRYPT_MIN_CLIENT_R);

    ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime fR=%f rRemainder=%f",fR,
                   rRemainder);

    if (fR > (double) SCRYPT_MIN_CLIENT_R)
    {
        fR = (double) SCRYPT_MIN_CLIENT_R;

        fEstTargetTimeElapsed *= (double) SCRYPT_MIN_CLIENT_R;
        fN = ((double) SCRYPT_TARGET_USECONDS / fEstTargetTimeElapsed);

        if (fN < 2.0)
            fR += rRemainder;

        if (fN > maxNShift)
        {
            fN = maxNShift;

            fEstTargetTimeElapsed *= maxNShift;

            fP = ((double) SCRYPT_TARGET_USECONDS / fEstTargetTimeElapsed);
        }
    }
    else
    {
        fR = (double) SCRYPT_MIN_CLIENT_R;
    }
    fN = fN >= 1.0 ? fN : 1.0;

    unsigned long nShift = ((SCRYPT_DEFAULT_CLIENT_N_SHIFT - 1) +
                            (unsigned long) fN);
    r = (unsigned long) fR;
    p = (unsigned long) fP;

    // Sanity check to make sure memory requirements don't go over 512MB
    for (; nShift > 1; nShift--)
    {
        bool breakout = false;
        if (r == 0) r = 1;
        for (; r >= 1; r--)
        {
            unsigned long long nTemp = (1 << nShift);
            // 512MB = 0x1F400000
            if ((128 * nTemp * r) > 0x1F400000)
            {
                ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime N*r too high. lowering r=%ul",
                               (unsigned long) r);
                continue;
            }

            breakout = true;
            break;
        }
        if (breakout)
            break;
        ABC_DebugLevel(1,
                       "ScryptSnrp::createSnrpFromTime N*r too high. lowering nShift=%ul",
                       (unsigned long) nShift);
    }

    // Sanity check to make sure r * p < 2^30
    for (; r > 1; r--)
    {
        bool breakout = false;
        if (p == 0) p = 1;
        for (; p >= 1; p--)
        {
            // 2^30 = 0x40000000
            if ( r * nShift > 0x40000000)
            {
                ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime p*r too high. lowering r=%ul",
                               (unsigned long) r);
                continue;
            }
            breakout = true;
            break;
        }
        if (breakout)
            break;
        ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime p*r too high. lowering p=%ul",
                       (unsigned long) p);
        continue;
    }
    if (r == 0) r = 1;
    if (p == 0) p = 1;
    if (nShift == 0) nShift = 1;

    n = 1 << nShift;

    ABC_DebugLevel(1, "ScryptSnrp::createSnrpFromTime time=%d Nrp=%llu %lu %lu\n\n",
                   totalTime,
                   (unsigned long long)n,
                   (unsigned long) r,
                   (unsigned long) p);
}

Status
ScryptSnrp::create()
{
    // Set up default values:
    ABC_CHECK(randomData(salt, SCRYPT_DEFAULT_SALT_LENGTH));
    n = SCRYPT_DEFAULT_CLIENT_N;
    r = SCRYPT_DEFAULT_CLIENT_R;
    p = SCRYPT_DEFAULT_CLIENT_P;

    // Benchmark the CPU:
    DataChunk temp;
    unsigned long totalTime;

    ABC_CHECK(hash(temp, salt, &totalTime));

    createSnrpFromTime(totalTime);

    return Status();
}

Status
ScryptSnrp::hash(DataChunk &result, DataSlice data, unsigned long *time,
                 size_t size) const
{
    DataChunk out(size);

    struct timeval timerStart;
    struct timeval timerEnd;
    gettimeofday(&timerStart, nullptr);
    int rc = crypto_scrypt(data.data(), data.size(),
                           salt.data(), salt.size(), n, r, p, out.data(), size);
    gettimeofday(&timerEnd, nullptr);

    // Find the time in microseconds:
    unsigned long totalTime = 1000000 * (timerEnd.tv_sec - timerStart.tv_sec);
    totalTime += timerEnd.tv_usec;
    totalTime -= timerStart.tv_usec;

    ABC_DebugLevel(1, "ScryptSnrp::hash Nrp=%llu %lu %lu time=%lu",
                   (unsigned long long) n, (unsigned long) r, (unsigned long) p, totalTime);

    if (time)
        *time = totalTime;

    if (rc)
        return ABC_ERROR(ABC_CC_ScryptError, "Error calculating Scrypt hash");

    result = std::move(out);
    return Status();
}

const ScryptSnrp &
usernameSnrp()
{
    static const ScryptSnrp mainnet =
    {
        {
            0xb5, 0x86, 0x5f, 0xfb, 0x9f, 0xa7, 0xb3, 0xbf,
            0xe4, 0xb2, 0x38, 0x4d, 0x47, 0xce, 0x83, 0x1e,
            0xe2, 0x2a, 0x4a, 0x9d, 0x5c, 0x34, 0xc7, 0xef,
            0x7d, 0x21, 0x46, 0x7c, 0xc7, 0x58, 0xf8, 0x1b
        },
        SCRYPT_DEFAULT_SERVER_N,
        SCRYPT_DEFAULT_SERVER_R,
        SCRYPT_DEFAULT_SERVER_P
    };
    static const ScryptSnrp testnet =
    {
        {
            0xa5, 0x96, 0x3f, 0x3b, 0x9c, 0xa6, 0xb3, 0xbf,
            0xe4, 0xb2, 0x36, 0x42, 0x37, 0xfe, 0x87, 0x1e,
            0xf2, 0x2a, 0x4a, 0x9d, 0x4c, 0x34, 0xa7, 0xef,
            0x3d, 0x21, 0x47, 0x8c, 0xc7, 0x58, 0xf8, 0x1b
        },
        SCRYPT_DEFAULT_SERVER_N,
        SCRYPT_DEFAULT_SERVER_R,
        SCRYPT_DEFAULT_SERVER_P
    };

    return isTestnet() ? testnet : mainnet;
}

} // namespace abcd
