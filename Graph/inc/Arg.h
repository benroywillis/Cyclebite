#include "DataValue.h"
#include <llvm/IR/Argument.h>

namespace Cyclebite::Graph
{
    class Arg : public DataValue 
    {
    public:
        Arg(const llvm::Argument* arg) : DataValue(arg), a(arg) {}
        ~Arg() = default;
        const llvm::Argument* getArg() const;
    private:
        const llvm::Argument* a;
    };
} // namespace Cyclebite::Graph