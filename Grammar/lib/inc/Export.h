#pragma once
#include <memory>

namespace Cyclebite::Grammar
{
    class Task;
    class Expression;
    void Export( const std::shared_ptr<Task>& t, const std::shared_ptr<Expression>& expr );
} // namespace Cyclebite::Grammar