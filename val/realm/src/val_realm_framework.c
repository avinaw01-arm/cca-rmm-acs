/*
 * Copyright (c) 2023-2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "val_realm_framework.h"
#include "val_smc.h"
#include "test_database.h"
#include "val_irq.h"
#include "val_realm_rsi.h"
#include "val.h"
#include "val_realm_memory.h"
#include "val_realm_planes.h"

extern uint64_t realm_ipa_width;
extern uint64_t val_image_load_offset;
extern const test_db_t test_list[];
extern bool realm_in_p0;
extern bool realm_in_pn;

/**
 *   @brief    Return secondary cpu entry address
 *   @param    void
 *   @return   Secondary cpu entry address
**/
uint64_t val_realm_get_secondary_cpu_entry(void)
{
    return (uint64_t)&acs_realm_entry;
}

/**
 *   @brief    Sends control back to host by executing hvc #n
 *   @param    void
 *   @return   void
**/
void val_realm_return_to_host(void)
{
    val_realm_rsi_host_call(VAL_SWITCH_TO_HOST);
    //val_return_to_host_hvc_asm();
}

/**
 *   @brief    Query test database and execute test from each suite one by one
 *   @param    void
 *   @return   void
**/
static void val_realm_test_dispatch(void)
{
    test_fptr_t       fn_ptr;

    fn_ptr = (test_fptr_t)(test_list[val_get_curr_test_num()].realm_fn);
    if (fn_ptr == NULL)
    {
        LOG(ERROR, "Invalid realm test address\n");
        pal_terminate_simulation();
    }

    /* Fix symbol relocation - Add image offset */
    fn_ptr = (test_fptr_t)(fn_ptr + val_image_load_offset);
    /* Execute realm test */
    fn_ptr();

    /* Control shouldn't come here. Test must send control back to host */
}

/**
 *   @brief    Check if realm executing in p0
 *   @param    none
 *   @return   TRUE/FALSE
**/
uint64_t val_realm_in_p0(void)
{
    return realm_in_p0;
}

/**
 *   @brief    Check if realm executing in pn
 *   @param    none
 *   @return   TRUE/FALSE
**/
uint64_t val_realm_in_pn(void)
{
    return realm_in_pn;
}

/**
 *   @brief    Determines the current executing plane inside the realm and sets respective flag.
 *   @param    none
 *   @return   SUCCESS/FAILURE
**/
static uint64_t val_realm_init(void)
{
    val_realm_rsi_version_ts val;
    uint64_t ret = val_realm_rsi_version(RSI_ABI_VERSION, &val);

    /* Determine the current executing plane based on the return value of RSI ABI */
    if (ret == RSI_SUCCESS) {
        realm_in_p0 = true;
    } else if (ret == VAL_SMC_NOT_SUPPORTED) {
        realm_in_pn = true;
    } else {
        return VAL_ERROR;
    }

    return VAL_SUCCESS;
}

/**
 *   @brief    Passes relam IPA width and initializes Common VAL functionalities.
 *   @param    ipa_width      - Realm IPA width
 *   @return   Void
**/
static void val_realm_common_val_init(uint64_t ipa_width)
{
    val_base_addr_ipa(ipa_width);
}

/**
 *   @brief    C entry function for endpoint
 *   @param    primary_cpu_boot     - Boolean value for primary cpu boot
 *   @return   void (Never returns)
**/
void val_realm_main(bool primary_cpu_boot)
{
    if (val_realm_init())
        goto shutdown;

    val_set_running_in_realm_flag();
    val_set_security_state_flag(SEC_STATE_REALM);

    uint64_t ipa_width = val_realm_get_ipa_width();
    val_realm_common_val_init(ipa_width);

    xlat_ctx_t  *realm_xlat_ctx = val_realm_get_xlat_ctx();

    if (primary_cpu_boot == true)
    {
        /* Add realm region into TT data structure */
        val_realm_add_mmap();

        val_realm_update_xlat_ctx_ias_oas(((1UL << ipa_width) - 1), ((1UL << ipa_width) - 1));
        /* Write page tables */
        val_setup_mmu(realm_xlat_ctx);

    }

    /* Enable Stage-1 MMU */
    val_enable_mmu(realm_xlat_ctx);

    val_irq_setup();
    /* Ready to run test regression */
    val_realm_test_dispatch();

shutdown:
    LOG(ALWAYS, "REALM : Entering standby.. \n");
    pal_terminate_simulation();
}
