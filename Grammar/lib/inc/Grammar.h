//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/Support/CommandLine.h>

namespace Cyclebite::Grammar
{
    extern llvm::cl::list<std::string> SourceFiles;
} // namespace Cyclebite::Grammar