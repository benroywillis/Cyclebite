#pragma once
#include <cstdint>
#include <memory>

namespace Cyclebite::Profile::Backend::Memory
{
    class UniqueID
    {
    public:
        /// Unique identifier
        uint64_t IID;
        UniqueID();
        virtual ~UniqueID();
        uint64_t getNextIID();

    private:
        /// Counter for the next unique idenfitfier
        static uint64_t nextIID;
        void setNextIID(uint64_t next);
    };

    struct UIDCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<UniqueID>& lhs, const std::shared_ptr<UniqueID>& rhs) const
        {
            return lhs->IID < rhs->IID;
        }
        bool operator()(const std::shared_ptr<UniqueID>& lhs, uint64_t rhs) const
        {
            return lhs->IID < rhs;
        }
        bool operator()(uint64_t lhs, const std::shared_ptr<UniqueID>& rhs) const
        {
            return lhs < rhs->IID;
        }
    };
} // namespace Cyclebite::Profile::Backend