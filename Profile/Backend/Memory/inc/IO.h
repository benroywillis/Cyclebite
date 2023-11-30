//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <string>
namespace Cyclebite::Profile::Backend::Memory
{
    void ReadKernelFile();
    std::string GenerateInstanceDot();
    std::string GenerateTaskOnlyInstanceDot();
    void GenerateTaskGraph();
    void GenerateTaskOnlyTaskGraph();
    void OutputKernelInstances();
} // namespace Cyclebite::Profile::Backend::Memory