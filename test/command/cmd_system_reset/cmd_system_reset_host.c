/*
 * Copyright (c) 2023, 2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "test_database.h"
#include "val_host_rmi.h"


void cmd_system_reset_host(void)
{
    val_host_realm_ts realm;
    uint64_t ret, i = 0;
    val_host_rec_exit_ts *rec_exit = NULL;

    val_memset(&realm, 0, sizeof(realm));

    val_host_realm_params(&realm);

    /* Populate realm with one REC*/
    if (val_host_realm_setup(&realm, 1))
    {
        LOG(ERROR, "Realm setup failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto destroy_realm;
    }

    rec_exit =  &(((val_host_rec_run_ts *)realm.run[0])->exit);

    /* Enter REC[0]  */
    ret = val_host_rmi_rec_enter(realm.rec[0], realm.run[0]);
    if (ret)
    {
        LOG(ERROR, "Rec enter failed, ret=%x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
        goto destroy_realm;
    }

    /* Calling PSCI_SYSTEM_RESET from realm should cause REC exit due to PSCI and exit.gprs
     * contains parameters from PSCI call */
    if (
            ((rec_exit->exit_reason) != RMI_EXIT_PSCI) ||
            /* gprs[0] holds the function is of psci call*/
            ((rec_exit->gprs[0]) != PSCI_SYSTEM_RESET)
       )
    {
        LOG(ERROR, "Rec Exit not due to  PSCI_SYSTEM_RESET\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
        goto destroy_realm;
    }

    LOG(TEST, "Check %2d : Positive Observability\n", ++i);

    /* REC enter should fail as the realm state is SYSTEM OFF */
    ret = val_host_rmi_rec_enter(realm.rec[0], realm.run[0]);
    if (ret != PACK_CODE(RMI_ERROR_REALM, 1))
    {
        LOG(ERROR, " Positive observability failed, ret=%x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
        goto destroy_realm;
    }

    val_set_status(RESULT_PASS(VAL_SUCCESS));

    /* Free test resources */
destroy_realm:
    return;
}
