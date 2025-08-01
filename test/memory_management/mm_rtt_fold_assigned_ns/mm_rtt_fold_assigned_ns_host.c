/*
 * Copyright (c) 2023, 2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "test_database.h"
#include "val_host_rmi.h"
#include "mm_common_host.h"

#define ALIGNED_2MB 0x200000
#define IPA_ALIGNED_2MB 0x800000

void mm_rtt_fold_assigned_ns_host(void)
{
    val_host_realm_ts realm;
    val_host_rtt_entry_ts rtte;
    val_host_rtt_entry_ts b_fold_rtt, a_fold_rtt;
    val_host_rtt_entry_ts unfold_rtt;
    uint64_t ret, rtt = 0;
    uint64_t phys, ipa, size;
    uint64_t i = 0, p_rtte_addr;

    val_memset(&realm, 0, sizeof(realm));

    val_host_realm_params(&realm);

    /* Populate realm with one REC */
    if (val_host_realm_setup(&realm, false))
    {
        LOG(ERROR, "Realm setup failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
        goto destroy_realm;
    }

    size = 0x200000;
    phys = (uint64_t)val_host_mem_alloc(ALIGNED_2MB, size);
    if (!phys)
    {
        LOG(ERROR, "val_host_mem_alloc failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
        goto destroy_realm;
    }

    ipa = (1ULL << (realm.s2sz - 1)) + IPA_ALIGNED_2MB;
    while (i < size/PAGE_SIZE)
    {
        ret = val_host_map_unprotected(&realm,
                                       phys + i * PAGE_SIZE,
                                       ipa + i * PAGE_SIZE,
                                       PAGE_SIZE, ALIGNED_2MB);
        if (ret)
        {
            LOG(ERROR, " val_host_map_unprotected failed\n");
            val_set_status(RESULT_FAIL(VAL_ERROR_POINT(3)));
            goto destroy_realm;
        }
        i++;
    }

    /* Save the parent rtte.addr for comparision after fold */
    ret = val_host_rmi_rtt_read_entry(realm.rd, ipa, VAL_RTT_MAX_LEVEL - 1, &rtte);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(4)));
        goto destroy_realm;
    }

    p_rtte_addr = OA(rtte.desc);

    /* Save the fold.addr and fold.ripas for comparision after fold */
    ret = val_host_rmi_rtt_read_entry(realm.rd, ipa, VAL_RTT_MAX_LEVEL, &b_fold_rtt);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(5)));
        goto destroy_realm;
    }

    ret = val_host_rmi_rtt_fold(realm.rd, ipa, VAL_RTT_MAX_LEVEL, &rtt);
    if (ret)
    {
        LOG(ERROR, "rmi_rtt_fold failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(6)));
        goto destroy_realm;
    }

    if (rtt != p_rtte_addr)
    {
        LOG(ERROR, "Fold rtt addr mismatch, expected %lx received %lx\n",
                                    p_rtte_addr, rtt);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(7)));
        goto destroy_realm;
    }

    /* Activate realm */
    if (val_host_realm_activate(&realm))
    {
        LOG(ERROR, "Realm activate failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(8)));
        goto destroy_realm;
    }

    /* Enter REC[0] execution */
    ret = val_host_rmi_rec_enter(realm.rec[0], realm.run[0]);
    if (ret)
    {
        LOG(ERROR, "Rec enter failed, ret=%x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(9)));
        goto destroy_realm;
    } else if (val_host_check_realm_exit_host_call((val_host_rec_run_ts *)realm.run[0]))
    {
        LOG(ERROR, "Host call params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(10)));
        goto destroy_realm;
    }

    /* Compare the state and attributes after the FOLD */
    ret = val_host_rmi_rtt_read_entry(realm.rd, ipa, VAL_RTT_MAX_LEVEL, &a_fold_rtt);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(11)));
        goto destroy_realm;
    }

    if (b_fold_rtt.state != a_fold_rtt.state ||
        b_fold_rtt.desc != a_fold_rtt.desc ||
        b_fold_rtt.ripas != a_fold_rtt.ripas ||
        b_fold_rtt.walk_level != (a_fold_rtt.walk_level + 1))
    {
        LOG(ERROR, "FOLD Rtt params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(12)));
        goto destroy_realm;
    }

    ret = val_host_rmi_rtt_read_entry(realm.rd, (ipa + 0x80000),
                                                VAL_RTT_MAX_LEVEL, &a_fold_rtt);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(13)));
        goto destroy_realm;
    }

    if (b_fold_rtt.state != a_fold_rtt.state ||
        b_fold_rtt.ripas != a_fold_rtt.ripas ||
        b_fold_rtt.desc != a_fold_rtt.desc ||
        b_fold_rtt.walk_level != (a_fold_rtt.walk_level + 1))
    {
        LOG(ERROR, "FOLD Rtt params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(14)));
        goto destroy_realm;
    }

    ret = val_host_create_rtt_levels(&realm, ipa,
                                    (uint32_t)rtte.walk_level, VAL_RTT_MAX_LEVEL, ALIGNED_2MB);
    if (ret)
    {
        LOG(ERROR, "val_host_create_rtt_level failed\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(15)));
        goto destroy_realm;
    }

    ret = val_host_rmi_rtt_read_entry(realm.rd, ipa, VAL_RTT_MAX_LEVEL, &unfold_rtt);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(16)));
        goto destroy_realm;
    }

    if (val_memcmp(&b_fold_rtt, &unfold_rtt, sizeof(b_fold_rtt)))
    {
        LOG(ERROR, "UNFOLD Rtt params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(17)));
        goto destroy_realm;
    }

    ret = val_host_rmi_rtt_read_entry(realm.rd, (ipa + 0x80000),
                                                VAL_RTT_MAX_LEVEL, &unfold_rtt);
    if (ret)
    {
        LOG(ERROR, "rtt_read_entry failed ret = %x\n", ret);
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(18)));
        goto destroy_realm;
    }

    if (b_fold_rtt.state != unfold_rtt.state ||
        b_fold_rtt.ripas != unfold_rtt.ripas ||
        (b_fold_rtt.desc + 0x80000) != unfold_rtt.desc ||
        b_fold_rtt.walk_level != unfold_rtt.walk_level)
    {
        LOG(ERROR, "UNFOLD Rtt params mismatch\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(19)));
        goto destroy_realm;
    }

    val_set_status(RESULT_PASS(VAL_SUCCESS));

    /* Free test resources */
destroy_realm:
    return;
}
