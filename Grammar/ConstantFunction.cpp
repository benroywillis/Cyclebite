#include "ConstantFunction.h"

using namespace std;
using namespace TraceAtlas::Grammar;

string ConstantFunction::dump() const
{
    string funcCall = string(f->getName())+"( ";
    uint32_t argIndex = 0;
    for( auto argi = f->arg_begin(); argi != f->arg_end(); argi++ )
    {
        switch( argi->getType()->getTypeID() )
        {
            case llvm::Type::VoidTyID: funcCall += "void arg"+to_string(argIndex)+",";
            case llvm::Type::IntegerTyID:
                switch( llvm::cast<llvm::IntegerType>(argi->getType())->getIntegerBitWidth() )
                {
                    case 1:  funcCall += "bool arg"+to_string(argIndex);
                    case 8:  funcCall += "int8 arg"+to_string(argIndex);
                    case 16: funcCall += "int16 arg"+to_string(argIndex);
                    case 32: funcCall += "int32 arg"+to_string(argIndex);
                    case 64: funcCall += "int64 arg"+to_string(argIndex);
                    default: funcCall += "nan arg"+to_string(argIndex);
                }
            case llvm::Type::FloatTyID:    funcCall += "float arg"+to_string(argIndex);
            case llvm::Type::DoubleTyID:   funcCall += "double arg"+to_string(argIndex);
            case llvm::Type::PointerTyID:  funcCall += "pointer arg"+to_string(argIndex);
            case llvm::Type::FunctionTyID: funcCall += "func arg"+to_string(argIndex);
            case llvm::Type::StructTyID:   funcCall += "struct arg"+to_string(argIndex);
            case llvm::Type::ArrayTyID:    funcCall += "array arg"+to_string(argIndex);
            default: funcCall += "arg"+to_string(argIndex);
        }
        if( argi != prev(f->arg_end()) )
        {
            funcCall += ",";
        }
        argIndex++;
   }
    funcCall += " )";

    return funcCall;
}

const llvm::Function* ConstantFunction::getFunction() const
{
    return f;
}