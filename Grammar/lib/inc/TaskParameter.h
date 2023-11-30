//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "Graph/inc/DataValue.h"

namespace Cyclebite::Grammar
{
    class Task;

    /// @brief Describes symbols that are required to complete a task expression, but do not themselves come from the task
    ///
    /// This only describes the symbols in the function expression, it does not include other outside values like base pointers
    class TaskParameter : public Symbol
    {
    public:
        TaskParameter(const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Task>& ta) : Symbol("param"), node(n), t(ta) {}
        ~TaskParameter() = default;
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        /// @brief Returns the task this parameter is used within 
        /// @return A pointer to the task
        const std::shared_ptr<Task>& getUser() const;
        std::string dump() const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        std::shared_ptr<Task> t;
    };
} // namespace Cyclebite::Grammar