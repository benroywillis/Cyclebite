#pragma once
#include <string>
#include <memory>

namespace TraceAtlas::Grammar
{
    class Symbol 
    {
    public:
        uint64_t getID() const;
        Symbol() = delete;
        virtual ~Symbol() = default;
        virtual std::string dump() const;
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
} // namespace TraceAtlas::Grammar