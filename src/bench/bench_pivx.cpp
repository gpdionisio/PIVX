// Copyright (c) 2015-2020 The Bitcoin Core developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"

#include "crypto/bls.h"
#include "key.h"
#include "util/system.h"

void CleanupBLSTests();
void CleanupBLSDkgTests();

int main(int argc, char** argv)
{
    ECC_Start();
    BLSInit();
    SetupEnvironment();
    g_logger->m_print_to_file = false; // don't want to write to debug.log file

    benchmark::BenchRunner::RunAll();

    // need to be called before global destructors kick in (PoolAllocator is needed due to many BLSSecretKeys)
    CleanupBLSDkgTests();
    CleanupBLSTests();

    ECC_Stop();
}
