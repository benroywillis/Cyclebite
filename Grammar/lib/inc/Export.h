#pragma once
#include <memory>

namespace Cyclebite::Grammar
{
    class Expression;
    void Export( std::shared_ptr<Expression>& expr );
} // namespace Cyclebite::Grammar