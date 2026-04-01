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
#include <fstream>
#include <string>

TEST(FirstTests, Test001)
{
    ASSERT_TRUE(true);
}

TEST(FirstTests, TestReadDataFile)
{
    std::ifstream file("test/data.txt");
    ASSERT_TRUE(file.is_open());
    std::string content;
    std::getline(file, content);
    ASSERT_EQ(content, "dummy data");
}
