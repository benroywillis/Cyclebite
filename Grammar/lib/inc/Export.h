//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <memory>
#include <map>
#include <vector>
#include <set>
#include <llvm/IR/Constant.h>

namespace Cyclebite::Grammar
{
    class Task;
    class Expression;
    class ConstantSymbol;
    /// Maps each constant parameter essential to the tasks to its symbol (this is used in the export operations to print the global at the top of the export)
    extern std::map<const llvm::Constant*, std::set<std::shared_ptr<ConstantSymbol>>> constants;
    /// @brief Facilitates output generation of various formats
    /// @param expr   A map from each task to output expression name
    /// @param name   The name used for all file names, generator names (in the case of Halide), etc
    /// @param labels Enable the labeling of each task. Cyclebite-Template will output the label of each task fully characterized by the flow to stdout.
    /// @param OMP    Enable OMP pragma generation. Each source file used in the input application will be annotated with OMP pragmas where parallelizable tasks were found.
    /// @param Halide Enable halide generator output
    void Export( const std::map<std::shared_ptr<Task>, 
                 std::vector<std::shared_ptr<Expression>>>& expr, 
                 std::string name = "", 
                 bool labels = true, 
                 bool OMP = true, 
                 bool Halide = true );
} // namespace Cyclebite::Grammar