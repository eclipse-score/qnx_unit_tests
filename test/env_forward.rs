// *******************************************************************************
// Copyright (c) 2026 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// https://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0
// *******************************************************************************

// Proves the host -> guest environment forwarding mechanism (QNX_FORWARD_ENV).
//
// Under the run-under-qnx-* configs the host value of QNX_FWD_ENV_CHECK is
// forwarded into the QNX guest by run_under_qnx.sh (writes cc_test_qnx_env.txt)
// and re-exported by prepare_test.sh. The expected value is injected on the
// host via --test_env in .bazelrc. See README "Forwarding Host Environment
// Variables".
//
// When the variable is not set (e.g. plain host `bazel test //...`, where no
// forwarding happens) the test returns early (Rust's test harness has no skip)
// so it stays green in every configuration.
#[cfg(test)]
mod tests {
    use std::env;

    #[test]
    fn forwarded_variable_is_visible_in_guest() {
        match env::var("QNX_FWD_ENV_CHECK") {
            Ok(value) => assert_eq!(value, "forwarded-into-qnx-guest"),
            Err(_) => eprintln!(
                "QNX_FWD_ENV_CHECK not set; environment forwarding is not \
                 exercised in this configuration."
            ),
        }
    }
}
