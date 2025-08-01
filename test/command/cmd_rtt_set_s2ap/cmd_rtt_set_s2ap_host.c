/*
 * Copyright (c) 2024-2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "test_database.h"
#include "val_host_rmi.h"
#include "rmi_rtt_set_s2ap_data.h"
#include "command_common_host.h"

#define IPA_WIDTH 40
#define L3_SIZE PAGE_SIZE
#define L2_SIZE (512 * L3_SIZE)
#define L1_SIZE (512 * L2_SIZE)

#define NUM_REALMS 2
#define VALID_REALM 0
#define INVALID_REALM 1

static val_host_realm_ts realm[NUM_REALMS];
static val_host_rec_exit_ts *rec_exit;

static struct argument_store {
    uint64_t rd_valid;
    uint64_t rec_ptr_valid;
    uint64_t base_valid;
    uint64_t top_valid;
} c_args;

struct arguments {
    uint64_t rd;
    uint64_t rec_ptr;
    uint64_t base;
    uint64_t top;
};

static uint64_t g_rec_other_owner_prep_sequence(void)
{
    val_host_rec_params_ts rec_params;
    val_host_realm_flags1_ts realm_flags;

    val_memset(&realm_flags, 0, sizeof(realm_flags));
    val_memset(&realm[INVALID_REALM], 0, sizeof(realm[INVALID_REALM]));

    val_host_realm_params(&realm[INVALID_REALM]);

    realm[INVALID_REALM].num_aux_planes = 1;
    realm_flags.rtt_tree_pp = RMI_FEATURE_TRUE;

    if (val_host_rmm_supports_rtt_tree_single())
        realm_flags.rtt_tree_pp = RMI_FEATURE_FALSE;

    realm[INVALID_REALM].s2sz = 40;
    realm[INVALID_REALM].hash_algo = RMI_HASH_SHA_256;
    realm[INVALID_REALM].s2_starting_level = 0;
    realm[INVALID_REALM].num_s2_sl_rtts = 1;
    realm[INVALID_REALM].vmid = 3;
    realm[INVALID_REALM].rec_count = 1;

    val_memcpy(&realm[INVALID_REALM].flags1, &realm_flags, sizeof(realm[INVALID_REALM].flags1));

    rec_params.pc = 0;
    rec_params.flags = RMI_RUNNABLE;


    if (val_host_realm_create_common(&realm[INVALID_REALM]))
    {
        LOG(ERROR, "Realm create failed\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    /* Populate realm with two RECs*/
    if (val_host_rec_create_common(&realm[INVALID_REALM], &rec_params))
    {
        LOG(ERROR, "REC Create Failed\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    return realm[INVALID_REALM].rec[0];
}

static uint64_t rd_valid_prep_sequence(void)
{
    uint64_t ret, ipa_base;
    val_host_realm_flags1_ts realm_flags;

    val_memset(&realm[VALID_REALM], 0, sizeof(realm[VALID_REALM]));
    val_memset(&realm_flags, 0, sizeof(realm_flags));

    val_host_realm_params(&realm[VALID_REALM]);

    realm[VALID_REALM].vmid = VALID_REALM;
    realm[VALID_REALM].rec_count = 2;
    realm[VALID_REALM].num_aux_planes = 2;
    realm_flags.rtt_tree_pp = RMI_FEATURE_TRUE;

    if (val_host_rmm_supports_rtt_tree_single())
        realm_flags.rtt_tree_pp = RMI_FEATURE_FALSE;

    val_memcpy(&realm[VALID_REALM].flags1, &realm_flags, sizeof(realm[VALID_REALM].flags1));

    /* Populate realm with two RECs*/
    if (val_host_realm_setup(&realm[VALID_REALM], 1))
    {
        LOG(ERROR, "Realm setup failed\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    /* Power on REC 1 for future execution */
    ret = val_host_rmi_rec_enter(realm[VALID_REALM].rec[0], realm[VALID_REALM].run[0]);
    if (ret)
    {
        LOG(ERROR, "REC_ENTER failed with ret value: %d\n", ret);
        return VAL_TEST_PREP_SEQ_FAILED;
    } else if (val_host_check_realm_exit_psci((val_host_rec_run_ts *)realm[VALID_REALM].run[0],
                                PSCI_CPU_ON_AARCH64))
    {
        LOG(ERROR, "REC exit not due to PSCI_CPU_ON\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    /* Complete pending PSCI by returning a RUNNABLE target REC*/
    ret = val_host_rmi_psci_complete(realm[VALID_REALM].rec[0], realm[VALID_REALM].rec[1],
                                                                             PSCI_E_SUCCESS);
    if (ret)
    {
        LOG(ERROR, " PSCI_COMPLETE Failed, ret=%x\n", ret);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    /* Enter REC[0] to continue execution */
    ret = val_host_rmi_rec_enter(realm[VALID_REALM].rec[0], realm[VALID_REALM].run[0]);
    if (ret)
    {
        LOG(ERROR, "REC_ENTER failed with ret value: %d\n", ret);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    rec_exit =  &(((val_host_rec_run_ts *)realm[VALID_REALM].run[0])->exit);
    ipa_base = rec_exit->s2ap_base;

    /* Check that REC exit was due to S2AP change request */
    if (rec_exit->exit_reason != RMI_EXIT_S2AP_CHANGE) {
        LOG(ERROR, "Unexpected REC exit, %d. ESR: %lx \n", rec_exit->exit_reason, rec_exit->esr);
        return VAL_TEST_PREP_SEQ_FAILED;
    }


    /* Create primary RTT mappings for requested IPA range in case it doesn't exist */
    while (ipa_base < rec_exit->s2ap_top)
    {
        if (create_mapping(ipa_base, false, realm[VALID_REALM].rd))
            return VAL_TEST_PREP_SEQ_FAILED;
        ipa_base += L2_SIZE;
    }

    /* If Realm is configured to have RTT tree per plane, create auxiliary RTT as well*/
    ipa_base = rec_exit->s2ap_base;
    if (realm_flags.rtt_tree_pp)
    {
        while (ipa_base < rec_exit->s2ap_top)
        {
            for (uint8_t i = 0; i < realm[VALID_REALM].num_aux_planes; i++)
            {
                if (val_host_create_aux_mapping(realm[VALID_REALM].rd, ipa_base, i + 1))
                    return VAL_TEST_PREP_SEQ_FAILED;
            }

            ipa_base += L2_SIZE;
        }
    }

    return realm[VALID_REALM].rd;
}

static uint64_t base_primary_unaligned_prep_sequence(void)
{
    uint64_t ret;

    ret = val_host_rmi_rec_enter(realm[VALID_REALM].rec[1], realm[VALID_REALM].run[1]);
    if (ret)
    {
        LOG(ERROR, "REC_ENTER failed with ret value: %d\n", ret);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    rec_exit =  &(((val_host_rec_run_ts *)realm[VALID_REALM].run[1])->exit);

    /* Check that REC exit was due S2AP change request */
    if (rec_exit->exit_reason != RMI_EXIT_S2AP_CHANGE) {
        LOG(ERROR, "Unexpected REC exit, %d. ESR: %lx \n", rec_exit->exit_reason, rec_exit->esr);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    return rec_exit->s2ap_base;
}

static uint64_t base_auxiliary_unaligned_prep_sequence(void)
{
    uint64_t ret, ipa_base;
    val_smc_param_ts cmd_ret;

    if (!VAL_EXTRACT_BITS(realm[VALID_REALM].flags1, 0, 0))
    {
        LOG(ALWAYS, "Realm is configured to use single RTT tree\n");
        return VAL_SKIP_CHECK;
    }

    ret = val_host_rmi_rec_enter(realm[VALID_REALM].rec[1], realm[VALID_REALM].run[1]);
    if (ret)
    {
        LOG(ERROR, "REC_ENTER failed with ret value: %d\n", ret);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    /* Check that REC exit was due S2AP change request */
    if (rec_exit->exit_reason != RMI_EXIT_S2AP_CHANGE) {
        LOG(ERROR, "Unexpected REC exit, %d. ESR: %lx \n", rec_exit->exit_reason, rec_exit->esr);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    ipa_base = rec_exit->s2ap_base;

    while (ipa_base < rec_exit->s2ap_top) {
        if (create_mapping(ipa_base, false, realm[VALID_REALM].rd))
            return VAL_TEST_PREP_SEQ_FAILED;
        ipa_base += PAGE_SIZE;
    }

    /* Walk RTT tree indices 0(primary) and 1 (plane 0's RTT tree) inorder for the test
     * to walk the RTT tree index 2 */
    ipa_base = rec_exit->s2ap_base;
    cmd_ret = val_host_rmi_rtt_set_s2ap(realm[VALID_REALM].rd,
                                realm[VALID_REALM].rec[1], ipa_base, rec_exit->s2ap_top);
    if (cmd_ret.x0) {
        LOG(ERROR, "RMI_SET_S2AP failed with ret= 0x%x\n", cmd_ret.x0);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    ipa_base = rec_exit->s2ap_base;
    cmd_ret = val_host_rmi_rtt_set_s2ap(realm[VALID_REALM].rd,
                                    realm[VALID_REALM].rec[1], ipa_base, rec_exit->s2ap_top);
    if (cmd_ret.x0) {
        LOG(ERROR, "RMI_SET_S2AP failed with ret= 0x%x\n", cmd_ret.x0);
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    return rec_exit->s2ap_base;
}

static uint64_t valid_input_args_prep_sequence(void)
{
    c_args.rd_valid = rd_valid_prep_sequence();
    if (c_args.rd_valid == VAL_TEST_PREP_SEQ_FAILED)
        return VAL_TEST_PREP_SEQ_FAILED;

    c_args.rec_ptr_valid = realm[VALID_REALM].rec[0];

    c_args.base_valid = rec_exit->s2ap_base;

    c_args.top_valid = rec_exit->s2ap_top;

    return VAL_SUCCESS;
}

static uint64_t intent_to_seq(struct stimulus *test_data, struct arguments *args)
{
    enum test_intent label = test_data->label;

    switch (label)
    {
        case RD_UNALIGNED:
            args->rd = g_unaligned_prep_sequence(c_args.rd_valid);
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_OUTSIDE_OF_PERMITTED_PA:
            args->rd = g_outside_of_permitted_pa_prep_sequence();
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_DEV_MEM_MMIO:
            args->rd = g_dev_mem_prep_sequence();
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_STATE_UNDELEGATED:
            args->rd = g_undelegated_prep_sequence();
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_STATE_DELEGATED:
            args->rd = g_delegated_prep_sequence();
            if (args->rd == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_STATE_REC:
            args->rd = realm[VALID_REALM].rec[0];
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_STATE_RTT:
            args->rd = realm[VALID_REALM].rtt_l0_addr;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case RD_STATE_DATA:
            args->rd = realm[VALID_REALM].image_pa_base;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_UNALIGNED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_unaligned_prep_sequence(c_args.rec_ptr_valid);
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_OUTSIDE_OF_PERMITTED_PA:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_outside_of_permitted_pa_prep_sequence();
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_DEV_MEM:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_dev_mem_prep_sequence();
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_GRAN_STATE_UNDELEGATED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_undelegated_prep_sequence();
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_GRAN_STATE_DELEGATED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_delegated_prep_sequence();
            if (args->rec_ptr == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_GRAN_STATE_RD:
            args->rd = c_args.rd_valid;
            args->rec_ptr = c_args.rd_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_GRAN_STATE_RTT:
            args->rd = c_args.rd_valid;
            args->rec_ptr = realm[VALID_REALM].rtt_l0_addr;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_GRAN_STATE_DATA:
            args->rd = c_args.rd_valid;
            args->rec_ptr = realm[VALID_REALM].image_pa_base;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case REC_OTHER_OWNER:
            args->rd = c_args.rd_valid;
            args->rec_ptr = g_rec_other_owner_prep_sequence();
            if (args->rec_ptr == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid;
            break;

        case INVALID_SIZE:
            args->rd = c_args.rd_valid;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.base_valid;
            break;

        case BASE_MISMATCH:
            args->rd = c_args.rd_valid;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid + 1;
            args->top = c_args.top_valid;
            break;

        case TOP_OUT_OF_BOUND:
            args->rd = c_args.rd_valid;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid + PAGE_SIZE;
            break;

        case TOP_GRAN_UNALIGNED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = c_args.rec_ptr_valid;
            args->base = c_args.base_valid;
            args->top = c_args.top_valid - PAGE_SIZE / 2;
            break;

        case BASE_PRIMARY_UNALIGNED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = realm[VALID_REALM].rec[1];
            args->base = base_primary_unaligned_prep_sequence();
            if (args->base == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            args->top = 2 * L2_SIZE;
            break;

        case BASE_AUXILIARY_UNALIGNED:
            args->rd = c_args.rd_valid;
            args->rec_ptr = realm[VALID_REALM].rec[1];
            args->base = base_auxiliary_unaligned_prep_sequence();
            if (args->base == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            else if (args->base == VAL_SKIP_CHECK)
                return VAL_SKIP_CHECK;
            args->top = 2 * L2_SIZE;
            break;

        default:
            LOG(ERROR, "Unknown intent label encountered\n");
            return VAL_ERROR;
    }

    return VAL_SUCCESS;
}

void cmd_rtt_set_s2ap_host(void)
{
    uint64_t i, base, ret;
    val_smc_param_ts cmd_ret;
    struct arguments args;

    /* Skip if RMM do not support planes */
    if (!val_host_rmm_supports_planes())
    {
        LOG(ALWAYS, "Planes feature not supported\n");
        val_set_status(RESULT_SKIP(VAL_SKIP_CHECK));
        goto exit;
    }

    if (valid_input_args_prep_sequence() == VAL_TEST_PREP_SEQ_FAILED) {
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto exit;
    }

    for (i = 0; i < (sizeof(test_data) / sizeof(struct stimulus)); i++)
    {
        LOG(TEST, "Check %2d : %s; intent id : 0x%x \n",
              i + 1, test_data[i].msg, test_data[i].label);

        ret = intent_to_seq(&test_data[i], &args);

        if (ret == VAL_SKIP_CHECK)
        {
            LOG(TEST, "Skipping Check : %d\n", i + 1);
            continue;
        }
        else if (ret == VAL_ERROR) {
            LOG(ERROR, " Intent to sequence failed \n");
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
            goto exit;
        }

        cmd_ret = val_host_rmi_rtt_set_s2ap(args.rd, args.rec_ptr, args.base, args.top);
        if (cmd_ret.x0 != PACK_CODE(test_data[i].status, test_data[i].index)) {
            LOG(ERROR, "Test Failure!The ABI call returned: %xExpected: %x\n",
                cmd_ret.x0, PACK_CODE(test_data[i].status, test_data[i].index));
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
            goto exit;
        }
    }

    LOG(TEST, "Check %2d : Positive Observability\n", ++i);

    base = c_args.base_valid;
    while (base != c_args.top_valid) {
        cmd_ret = val_host_rmi_rtt_set_s2ap(c_args.rd_valid, c_args.rec_ptr_valid,
                                                            c_args.base_valid, c_args.top_valid);
        if (cmd_ret.x0 != 0)
        {
            LOG(ERROR, " Command failed. %x\n", cmd_ret.x0);
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
            goto exit;
        }

        base = cmd_ret.x1;
    }

    val_set_status(RESULT_PASS(VAL_SUCCESS));

exit:
    return;
}
