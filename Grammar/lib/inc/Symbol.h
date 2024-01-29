//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <string>
#include <memory>
#include <map>

namespace Cyclebite::Grammar
{
    class Dimension;
    class ReductionVariable;
    class Symbol 
    {
    public:
        uint64_t getID() const;
        Symbol() = delete;
        virtual ~Symbol() = default;
        virtual std::string dump() const;
        virtual std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const;
        std::string getName() const;
    protected:
        uint64_t UID;
        static uint64_t nextUID;
        std::string name;
        static uint64_t getNextUID(); 
        Symbol(std::string n);
    };

    struct SymbolCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<Symbol>& lhs, const std::shared_ptr<Symbol>& rhs) const
        {
            return lhs->getID() < rhs->getID();
        }
        bool operator()(const std::shared_ptr<Symbol>& lhs, uint64_t rhs) const
        {
            return lhs->getID() < rhs;
        }
        bool operator()(uint64_t lhs, const std::shared_ptr<Symbol>& rhs) const
        {
            return lhs < rhs->getID();
        }
    };
} // namespace Cyclebite::Grammar