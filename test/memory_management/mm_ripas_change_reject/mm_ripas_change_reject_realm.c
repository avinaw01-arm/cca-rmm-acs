/*
 * Copyright (c) 2023-2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "test_database.h"
#include "val_realm_framework.h"
#include "val_realm_rsi.h"

void mm_ripas_change_reject_realm(void)
{
    uint64_t ipa_base, size;
    uint8_t ripas_val;
    val_realm_rsi_host_call_t *gv_realm_host_call;
    val_smc_param_ts args;
    uint64_t flags = RSI_NO_CHANGE_DESTROYED;

    /* Below code is executed for REC[0] only */
    LOG(DBG, "In realm_create_realm REC[0], mpdir=%x\n", val_read_mpidr());

    gv_realm_host_call = val_realm_rsi_host_call_ripas(VAL_SWITCH_TO_HOST);
    ipa_base = gv_realm_host_call->gprs[1];
    size = gv_realm_host_call->gprs[2];
    ripas_val = RSI_EMPTY;
    val_memset(&args, 0x0, sizeof(val_smc_param_ts));
    args = val_realm_rsi_ipa_state_set(ipa_base, ipa_base + size, ripas_val, flags);
    if (args.x0 || (args.x1 != ipa_base))
    {
        LOG(ERROR, "rsi_ipa_state_set failed x0 %lx x1 %lx\n", args.x0, args.x1);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto exit;
    }

    val_memset(&args, 0x0, sizeof(val_smc_param_ts));
    args = val_realm_rsi_ipa_state_get(ipa_base, ipa_base + size);
    if (args.x0 || (args.x2 != RSI_RAM))
    {
        LOG(ERROR, "rsi_ipa_state_get failed x0 %lx x2 %lx\n", args.x0, args.x2);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
    }

    ripas_val = RSI_EMPTY;
    val_memset(&args, 0x0, sizeof(val_smc_param_ts));
    args = val_realm_rsi_ipa_state_set(ipa_base, ipa_base + 0x2000, ripas_val, flags);
    if (args.x0 || (args.x1 != (ipa_base + 0x2000)))
    {
        LOG(ERROR, "rsi_ipa_state_set failed x0 %lx x1 %lx\n", args.x0, args.x1);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
        goto exit;
    }

    ripas_val = RSI_RAM;
    val_memset(&args, 0x0, sizeof(val_smc_param_ts));
    args = val_realm_rsi_ipa_state_set(ipa_base, ipa_base + size, ripas_val, flags);
    if (args.x0 || (args.x1 != (ipa_base + 0x2000)))
    {
        LOG(ERROR, "rsi_ipa_state_set failed x0 %lx x1 %lx\n", args.x0, args.x1);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
        goto exit;
    }

exit:
    val_realm_return_to_host();
}
