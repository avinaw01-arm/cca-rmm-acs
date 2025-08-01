/*
 * Copyright (c) 2024-2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "test_database.h"
#include "val_host_rmi.h"
#include "val_host_helpers.h"
#include "val_host_command.h"

#define TEST_IPA 0x1000

void planes_rec_exit_ia_ripas_destroyed_host(void)
{
    static val_host_realm_ts realm;
    val_host_realm_flags1_ts realm_flags;
    uint64_t ret;
    val_smc_param_ts cmd_ret;
    val_host_rec_exit_ts *rec_exit = NULL;
    val_host_rec_enter_ts *rec_enter = NULL;
    val_data_create_ts data_create;
    val_host_data_destroy_ts data_destroy;
    uint64_t phys;

    /* Skip if RMM do not support planes */
    if (!val_host_rmm_supports_planes())
    {
        LOG(ALWAYS, "Planes feature not supported\n");
        val_set_status(RESULT_SKIP(VAL_SKIP_CHECK));
        goto destroy_realm;
    }

    val_memset(&realm, 0, sizeof(realm));
    val_memset(&realm_flags, 0, sizeof(realm_flags));

    val_host_realm_params(&realm);

    /* Overwrite Realm Parameters */
    realm.num_aux_planes = 1;
    realm_flags.rtt_tree_pp = RMI_FEATURE_TRUE;

    if (val_host_rmm_supports_rtt_tree_single())
        realm_flags.rtt_tree_pp = RMI_FEATURE_FALSE;

    val_memcpy(&realm.flags1, &realm_flags, sizeof(realm.flags1));

    LOG(DBG, " INFO: RTT tree per plane : %d\n", realm_flags.rtt_tree_pp);
    /* Populate realm with one REC*/
    if (val_host_realm_setup(&realm, false))
    {
        LOG(ERROR, "Realm setup failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto destroy_realm;
    }

    /* Prepare IPA whose HIPAS = DESTROYED */
    data_create.size = PAGE_SIZE;
    phys = (uint64_t)val_host_mem_alloc(PAGE_SIZE, (2 * data_create.size));
    if (!phys)
    {
        LOG(ERROR, "val_host_mem_alloc failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
        goto destroy_realm;
    }

    data_create.src_pa = phys;
    data_create.target_pa = phys + data_create.size;
    data_create.ipa = TEST_IPA;
    data_create.rtt_alignment = PAGE_SIZE;
    ret = val_host_map_protected_data_to_realm(&realm, &data_create);
    if (ret)
    {
        LOG(ERROR, "val_host_map_protected_data_to_realm failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
        goto destroy_realm;
    }

    /* If Realm is configured to use RTT tree per plane, unmap IPA from auxiliary RTTs
     * before destroying from primary RTT */
    if (VAL_EXTRACT_BITS(realm.flags1, 0, 0) && realm.num_aux_planes > 0)
    {
        for (uint64_t i = 0; i < realm.num_aux_planes; i++)
        {

            cmd_ret = val_host_rmi_rtt_aux_unmap_protected(realm.rd, TEST_IPA, i + 1);

            if (cmd_ret.x0)
            {
                LOG(ERROR, "RTT_AUX_UNMAP_PRTOTECTED failed, ret=%d\n", cmd_ret.x0);
                val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
                goto destroy_realm;
            }
        }
    }

    ret = val_host_rmi_data_destroy(realm.rd, TEST_IPA, &data_destroy);
    if (ret)
    {
        LOG(ERROR, "Data destroy failed, ipa=0x%lx, ret=0x%x\n", TEST_IPA, ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(5)));
        goto destroy_realm;
    }

    /* Activate realm */
    if (val_host_realm_activate(&realm))
    {
        LOG(ERROR, "Realm activate failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(6)));
        goto destroy_realm;
    }

    rec_enter = &(((val_host_rec_run_ts *)realm.run[0])->enter);
    rec_exit = &(((val_host_rec_run_ts *)realm.run[0])->exit);

    /* Enter REC[0]  */
    ret = val_host_rmi_rec_enter(realm.rec[0], realm.run[0]);
    if (ret)
    {
        LOG(ERROR, "Rec enter failed, ret=%x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(7)));
        goto destroy_realm;
    }

    /* Check that REC Exit was due to host call because of P0 requesting for test IPA */
    if (rec_exit->exit_reason != RMI_EXIT_HOST_CALL) {
        LOG(ERROR, "Unexpected REC exit, %d. ESR: %lx \n", rec_exit->exit_reason, rec_exit->esr);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(8)));
        goto destroy_realm;
    }

    /* Return the test IPA to P0 */
    rec_enter->gprs[1] = TEST_IPA;
    rec_enter->gprs[2] = PAGE_SIZE;

    /* Enter REC[0]  */
    ret = val_host_rmi_rec_enter(realm.rec[0], realm.run[0]);
    if (ret)
    {
        LOG(ERROR, "Rec enter failed, ret=%x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(9)));
        goto destroy_realm;
    }

    /* Check that REC exit was due S2AP change request */
    if (rec_exit->exit_reason != RMI_EXIT_S2AP_CHANGE) {
        LOG(ERROR, "Unexpected REC exit, %d. ESR: %lx \n", rec_exit->exit_reason, rec_exit->esr);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(10)));
        goto destroy_realm;
    }

    /* Update S2AP for the requested memory range */
    if (val_host_set_s2ap(&realm))
    {
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(11)));
        goto destroy_realm;
    }

    /* Check that REC exit was due to Data Abort due to P1 access to IPA whose
     * RIPAS = DESTROYED */
    if (validate_rec_exit_ia(rec_exit, TEST_IPA))
    {
        LOG(ERROR, "REC exit IA: params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(12)));
        goto destroy_realm;
    }

    val_set_status(RESULT_PASS(VAL_SUCCESS));

    /* Free test resources */
destroy_realm:
    return;
}
