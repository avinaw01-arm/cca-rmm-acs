/*
 * Copyright (c) 2023, 2025, Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "test_database.h"

#define TEST_FUNC_DATABASE
#define HOST_TEST(x, y, z)              DUMMY_TEST(x, y, z)
#define HOST_REALM_TEST(x, y, z)        REALM_TEST_ONLY(x, y, z)
#define HOST_SECURE_TEST(x, y, z)       DUMMY_TEST(x, y, z)
#define HOST_REALM_SECURE_TEST(x, y, z) REALM_TEST_ONLY(x, y, z)

const test_db_t test_list[] = {
    {"", "", "", NULL, NULL, NULL},

#include "test_list.h"
    {"", "", "", NULL, NULL, NULL},

};

const uint32_t total_tests = sizeof(test_list)/sizeof(test_list[0]);
