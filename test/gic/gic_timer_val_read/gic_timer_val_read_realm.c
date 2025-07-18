/*
 * Copyright (c) 2023, 2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "test_database.h"
#include "val_realm_framework.h"
#include "val_realm_rsi.h"
#include "val_timer.h"

#define TIMEOUT     100

void gic_timer_val_read_realm(void)
{
    uint64_t phys_counter_t0, phys_counter_t1;
    uint64_t virt_counter_t0, virt_counter_t1;
    uint64_t timeout = TIMEOUT + 1;

    /* Below code is executed for REC[0] only */
    LOG(DBG, "In realm_create_realm REC[0], mpdir=%x\n", val_read_mpidr());
    val_timer_set_phy_el1(TIMEOUT, true);

    phys_counter_t0 = syscounter_read();
    virt_counter_t0 = virtualcounter_read();

    phys_counter_t1 = syscounter_read();
    while ((phys_counter_t0 == phys_counter_t1) && timeout > 0)
    {
        val_sleep_elapsed_time(1);
        timeout--;
        phys_counter_t1 = syscounter_read();
    }
    virt_counter_t1 = virtualcounter_read();


    if (phys_counter_t1 <= phys_counter_t0)
    {
        LOG(ERROR, "Physical counter is not increased\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(1)));
    }

    if (virt_counter_t1 <= virt_counter_t0)
    {
        LOG(ERROR, "virtual counter is not increased\n");
        val_set_status(RESULT_FAIL(VAL_ERROR_POINT(2)));
    }

    val_disable_phy_timer_el1();
    val_realm_return_to_host();
}
