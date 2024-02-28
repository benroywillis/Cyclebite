//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//

#include "FunctionCallArgs.h"
#include "Util/Exceptions.h"
#include <llvm/IR/Constants.h>
#include <llvm/ADT/APInt.h>

using namespace std;
using namespace Cyclebite::Grammar;

FunctionCallArgs::FunctionCallArgs() {}

FunctionCallArgs::FunctionCallArgs( const llvm::CallBase* call )
{
    for( unsigned i = 0; i < call->arg_size(); i++ )
    {
        if( const auto& con = llvm::dyn_cast<llvm::Constant>(call->getArgOperand(i)) )
        {
            if( con->getType()->isIntegerTy() )
            {
                if( llvm::cast<llvm::IntegerType>(con->getType())->getBitWidth() == 8 )
                {
                    member m;
                    m.b = (int8_t)*con->getUniqueInteger().getRawData();
                    types.push_back(FunctionCallArgs::T_member::INT8_T);
                    args.push_back( m );
                }
                else if( llvm::cast<llvm::IntegerType>(con->getType())->getBitWidth() == 16 )
                {
                    member m;
                    m.d = (int16_t)*con->getUniqueInteger().getRawData();
                    types.push_back(FunctionCallArgs::T_member::INT16_T);
                    args.push_back( m );
                }
                else if( llvm::cast<llvm::IntegerType>(con->getType())->getBitWidth() == 32 )
                {
                    member m;
                    m.f = (int32_t)*con->getUniqueInteger().getRawData();
                    types.push_back(FunctionCallArgs::T_member::INT32_T);
                    args.push_back( m );
                }
                else if( llvm::cast<llvm::IntegerType>(con->getType())->getBitWidth() == 64 )
                {
                    member m;
                    m.h = (int64_t)*con->getUniqueInteger().getRawData();
                    types.push_back(FunctionCallArgs::T_member::INT64_T);
                    args.push_back( m );
                }
            }
            else if( con->getType()->is16bitFPTy() )
            {
                if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
                {
                    member m;
                    m.i = (short)conF->getValueAPF().convertToFloat();
                    types.push_back(FunctionCallArgs::T_member::SHORT);
                    args.push_back( m );
                }
                else
                {
                    throw CyclebiteException("Could not extract float from constant float!");
                }
            }
            else if( con->getType()->isFloatTy() )
            {
                if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
                {
                    member m;
                    m.j = conF->getValueAPF().convertToFloat();
                    types.push_back(FunctionCallArgs::T_member::FLOAT);
                    args.push_back( m );
                }
                else
                {
                    throw CyclebiteException("Could not extract float from constant float!");
                }
            }
            else if( con->getType()->isDoubleTy() )
            {
                if( const auto& conD = llvm::dyn_cast<llvm::ConstantFP>(con) )
                {
                    member m;
                    m.k = conD->getValueAPF().convertToDouble();
                    types.push_back(FunctionCallArgs::T_member::DOUBLE);
                    args.push_back( m );
                }
                else
                {
                    throw CyclebiteException("Could not extract double from constant double!");
                }
            }
            else if( con->getType()->isPointerTy() )
            {
                // do nothing for now
            }
        }
        // else you have to search through the DFG to see if these values are resolvable
    }
}

void* FunctionCallArgs::getMember(unsigned i) const
{
    if( i >= types.size() )
    {
        return 0;
    }
    switch( types[i] )
    {
        case FunctionCallArgs::T_member::UINT8_T : return (void*)&args[i].a;
        case FunctionCallArgs::T_member::INT8_T  : return (void*)&args[i].b;
        case FunctionCallArgs::T_member::UINT16_T: return (void*)&args[i].c;
        case FunctionCallArgs::T_member::INT16_T : return (void*)&args[i].d;
        case FunctionCallArgs::T_member::UINT32_T: return (void*)&args[i].e;
        case FunctionCallArgs::T_member::INT32_T : return (void*)&args[i].f;
        case FunctionCallArgs::T_member::UINT64_T: return (void*)&args[i].g;
        case FunctionCallArgs::T_member::INT64_T : return (void*)&args[i].h;
        case FunctionCallArgs::T_member::SHORT   : return (void*)&args[i].i;
        case FunctionCallArgs::T_member::FLOAT   : return (void*)&args[i].j;
        case FunctionCallArgs::T_member::DOUBLE  : return (void*)&args[i].k;
        case FunctionCallArgs::T_member::VOID    : return (void*)&args[i].l;
    }
}