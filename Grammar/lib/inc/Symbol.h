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
        /// @brief Recursively implements the dump function for each symbol object specifically for halide
        ///
        /// Each symbol object must override this method.
        /// In each implementation, the function is recursive to the symbol2Symbol map
        /// - this allows arbitrary numbers of mappings to be followed in the map
        ///   -- e.g., { var 0 -> var1, var1 -> var2 } will result in var0 -> var2
        /// Thus, it is best to avoid circular references in the symbol2Symbol map (e.g., {var0 -> var0} results in an infinite recursion)
        /// @param symbol2Symbol A map of transformations (e.g., {var0 -> var1}). This map shall not contain circular references (e.g., {var0 -> var0})
        /// @return A string representing a valid halide expression for the object
        virtual std::string dumpHalide( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const;
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