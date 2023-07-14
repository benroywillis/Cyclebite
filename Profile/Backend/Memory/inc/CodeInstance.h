#pragma once
#include "UniqueID.h"
#include "Iteration.h"
#include <vector>

namespace Cyclebite::Profile::Backend::Memory
{
    class CodeInstance : public UniqueID
    {
    public:
        ~CodeInstance() = default;
        const Iteration& getMemory() const;
        void addIteration(const std::shared_ptr<Iteration>& newIteration);
        void addIteration(const Iteration& newIteration);
    protected:
        CodeInstance();
        /// List of iteration structures that are executed by this code
        Iteration memoryData;
    };
} // namespace Cyclebite::Profile::Backend