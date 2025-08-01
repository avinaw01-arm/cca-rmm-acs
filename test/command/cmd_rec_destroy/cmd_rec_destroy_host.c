/*
 * Copyright (c) 2023-2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "test_database.h"
#include "val_host_rmi.h"
#include "rmi_rec_destroy_data.h"
#include "command_common_host.h"

#define IPA_WIDTH 40
#define L3_SIZE PAGE_SIZE
#define L2_SIZE (512 * L3_SIZE)
#define L1_SIZE (512 * L2_SIZE)

#define NUM_REALMS 1
#define VALID_REALM 0
#define IPA_ADDR_DATA  5 * PAGE_SIZE

static val_host_realm_ts realm[NUM_REALMS];

static struct argument_store {
    uint64_t rec_ptr_valid;
} c_args;

struct arguments {
    uint64_t rec_ptr;
};

static uint64_t g_rec_aux_prep_sequence(void)
{
    /* Delegate granule for the REC */
    uint64_t rec = g_delegated_prep_sequence();
    if (rec == VAL_TEST_PREP_SEQ_FAILED)
        return VAL_TEST_PREP_SEQ_FAILED;

    /* Track for reuse/destruction */
    realm[VALID_REALM].rec[1] = rec;

    val_host_rec_params_ts *params = val_host_mem_alloc(PAGE_SIZE, PAGE_SIZE);

    uint64_t i;

    for (i = 0; i < (sizeof(params->gprs) / sizeof(params->gprs[0])); i++)
        params->gprs[i] = 0x0;

    /* Populate params structure */
    params->pc = 0;
    params->flags = RMI_RUNNABLE;
    params->mpidr = 1;

    uint64_t aux_count;
    uint64_t rd = realm[VALID_REALM].rd;

    if (val_host_rmi_rec_aux_count(rd, &aux_count))
        return VAL_TEST_PREP_SEQ_FAILED;

    params->num_aux = aux_count;

    /* Create all aux granules */
    for (i = 0; i < aux_count; i++) {
        uint64_t aux_rec = g_delegated_prep_sequence();
        if (aux_rec == VAL_TEST_PREP_SEQ_FAILED)
            return VAL_TEST_PREP_SEQ_FAILED;

        params->aux[i] = aux_rec;
    }

    /* Create the REC */
    if (val_host_rmi_rec_create(rd, rec, (uint64_t)params)) {
        LOG(ERROR, " REC create failed \n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    return params->aux[0];
}

static uint64_t rec_valid_prep_sequence(void)
{
    val_host_rec_params_ts rec_params;

    val_memset(&realm[VALID_REALM], 0, sizeof(realm[VALID_REALM]));
    val_memset(&rec_params, 0, sizeof(rec_params));

    realm[VALID_REALM].s2sz = 40;
    realm[VALID_REALM].hash_algo = RMI_HASH_SHA_256;
    realm[VALID_REALM].s2_starting_level = 0;
    realm[VALID_REALM].num_s2_sl_rtts = 1;
    realm[VALID_REALM].vmid = VALID_REALM;
    realm[VALID_REALM].rec_count = 1;

    if (val_host_realm_create_common(&realm[VALID_REALM]))
    {
        LOG(ERROR, "Realm create failed\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    if (val_host_rec_create_common(&realm[VALID_REALM], &rec_params))
    {
        LOG(ERROR, "Rec create failed\n");
        return VAL_TEST_PREP_SEQ_FAILED;
    }

    return realm[VALID_REALM].rec[0];
}

static uint64_t valid_input_args_prep_sequence(void)
{
    c_args.rec_ptr_valid = rec_valid_prep_sequence();
    if (c_args.rec_ptr_valid == VAL_TEST_PREP_SEQ_FAILED)
        return VAL_ERROR;

    return VAL_SUCCESS;
}

static uint64_t intent_to_seq(struct stimulus *test_data, struct arguments *args)
{
    enum test_intent label = test_data->label;

    switch (label)
    {
        case REC_UNALIGNED:
            args->rec_ptr = g_unaligned_prep_sequence(c_args.rec_ptr_valid);
            break;

        case REC_OUTSIDE_OF_PERMITTED_PA:
            args->rec_ptr = g_outside_of_permitted_pa_prep_sequence();
            break;

        case REC_DEV_MEM:
            args->rec_ptr = g_dev_mem_prep_sequence();
            break;

        case REC_GRAN_STATE_UNDELEGATED:
            args->rec_ptr = g_undelegated_prep_sequence();
            if (args->rec_ptr == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            break;

        case REC_GRAN_STATE_RD:
            args->rec_ptr = realm[VALID_REALM].rd;
            break;

        case REC_GRAN_STATE_REC_AUX:
            args->rec_ptr = g_rec_aux_prep_sequence();
            if (args->rec_ptr == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            break;

        case REC_GRAN_STATE_RTT:
            args->rec_ptr = realm[VALID_REALM].rtt_l0_addr;
            break;

        case REC_GRAN_STATE_DATA:
            args->rec_ptr = g_data_prep_sequence(realm[VALID_REALM].rd, IPA_ADDR_DATA);
            if (args->rec_ptr == VAL_TEST_PREP_SEQ_FAILED)
                return VAL_ERROR;
            break;

        default:
            LOG(ERROR, "Unknown intent label encountered\n");
            return VAL_ERROR;
    }

    return VAL_SUCCESS;
}

void cmd_rec_destroy_host(void)
{
    uint64_t ret, i;
    struct arguments args;

    if (valid_input_args_prep_sequence() == VAL_TEST_PREP_SEQ_FAILED) {
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto exit;
    }

    for (i = 0; i < (sizeof(test_data) / sizeof(struct stimulus)); i++)
    {
        LOG(TEST, "Check %2d : %s; intent id : 0x%x \n",
              i + 1, test_data[i].msg, test_data[i].label);

        if (intent_to_seq(&test_data[i], &args)) {
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
            goto exit;
        }

        ret = val_host_rmi_rec_destroy(args.rec_ptr);
        if (ret != PACK_CODE(test_data[i].status, test_data[i].index)) {
            LOG(ERROR, "Test Failure!The ABI call returned: %xExpected: %x\n",
                ret, PACK_CODE(test_data[i].status, test_data[i].index));
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
            goto exit;
        }
    }

    LOG(TEST, "Check %2d : Positive Observability\n", ++i);
    ret = val_host_rmi_rec_destroy(c_args.rec_ptr_valid);
    if (ret != 0)
    {
        LOG(ERROR, " REC destroy failed. %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
        goto exit;
    }

    val_set_status(RESULT_PASS(VAL_SUCCESS));

exit:
    return;
}
