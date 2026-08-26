/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#include <gtest/gtest.h>
#include <cstdlib>

// Proves the host -> guest environment forwarding mechanism (QNX_FORWARD_ENV).
//
// Under the run-under-qnx-* configs the host values of QNX_FWD_ENV_CHECK and
// QNX_FWD_ENV_CHECK_2 are forwarded into the QNX guest by run_under_qnx.sh
// (writes cc_test_qnx_env.sh) and re-exported by prepare_test.sh. The expected
// values are injected on the host via --test_env in .bazelrc. See README
// "Forwarding Host Environment Variables".
//
// Two variables are checked on purpose: the list in QNX_FORWARD_ENV is
// comma-separated, and an earlier implementation only ever forwarded the
// first entry.
//
// When the variable is not set (e.g. plain host `bazel test //...`, where no
// forwarding happens) the test skips so it stays green in every configuration.
TEST(EnvForwarding, ForwardedVariableIsVisibleInGuest)
{
    const char* value = std::getenv("QNX_FWD_ENV_CHECK");
    if (value == nullptr)
    {
        GTEST_SKIP() << "QNX_FWD_ENV_CHECK not set; environment forwarding is "
                        "not exercised in this configuration.";
    }
    EXPECT_STREQ(value, "forwarded-into-qnx-guest");

    // Second entry of the comma-separated list; its value contains a space, so
    // it also covers quoting of the forwarded value.
    const char* second = std::getenv("QNX_FWD_ENV_CHECK_2");
    ASSERT_NE(second, nullptr) << "QNX_FWD_ENV_CHECK was forwarded but "
                                 "QNX_FWD_ENV_CHECK_2 was not.";
    EXPECT_STREQ(second, "second forwarded value");
}
