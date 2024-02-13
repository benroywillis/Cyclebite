//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ConstantArray.h"
#include "IndexVariable.h"
#include "Util/Helpers.h"
#include "Util/Print.h"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>

using namespace Cyclebite::Grammar;
using namespace std;

template <typename T>
const vector<shared_ptr<IndexVariable>>& ConstantArray<T>::getVars() const
{
    return vars;
}

template <typename T>
const T* ConstantArray<T>::getArray() const
{
    return array;
}

template <typename T>
int ConstantArray<T>::getArraySize() const
{
    return arraySize;
}

template <typename T>
string ConstantArray<T>::dumpHalide( const map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>>& dimToRV ) const
{
    return this->dump();
}

template<typename T>
string ConstantArray<T>::dumpArray_C() const
{
    string type = string(typeid(T).name());
    string cArray = "const "+type+" "+this->name+" = { ";
    for( unsigned i = 0; i < arraySize; i++ )
    {
        if( i > 0 )
        {
            cArray += ", ";
        }
        cArray += to_string(array[i]);
    }
    cArray += " };";
    return cArray;
}

string getArrayType(const llvm::Constant* ptr)
{
    return Cyclebite::Util::PrintVal( Cyclebite::Util::getContainedType(ptr), false );
}

unsigned getArraySize(const llvm::ArrayType* t)
{
    // arrays can contain multiple dimensions of stuff, so we recur through them here, each time permuting the sizes that are found
    vector<unsigned> depths;
    deque<const llvm::Type*> Q;
    set<const llvm::Type*> covered;
    Q.push_front(t);
    covered.insert(t);
    while( !Q.empty() )
    {
        if( const auto& at = llvm::dyn_cast<llvm::ArrayType>(Q.front()) )
        {
            depths.push_back((unsigned)at->getNumElements());
            for( unsigned i = 0; i < at->getNumContainedTypes(); i++ )
            {
                Q.push_back(at->getContainedType(i));
            }
        }
        else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(Q.front()) )
        {
            depths.push_back((unsigned)vt->getArrayNumElements());
            for( unsigned i = 0; i < vt->getNumContainedTypes(); i++ )
            {
                Q.push_back(vt->getContainedType(i));
            }
        }
        Q.pop_front();
    }
    // now we permute the sizes of the dimensions of the array together to get its total size
    unsigned elems = 1;
    for( unsigned i = 0; i < depths.size(); i++ )
    {
        elems *= depths[i];
    }
    return elems;
}

const llvm::Type* getBaseType(const llvm::ArrayType* t)
{
    // we recur through the array dimensions until the "base" element type is found
    const llvm::Type* baseType = nullptr;
    deque<const llvm::Type*> Q;
    set<const llvm::Type*> covered;
    Q.push_front(t);
    covered.insert(t);
    while( !Q.empty() )
    {
        if( !(Q.front()->isAggregateType() || Q.front()->isStructTy() || Q.front()->isFunctionTy() || Q.front()->isPointerTy() || Q.front()->isTargetExtTy()) )
        {
            return Q.front();
        }
        if( const auto& at = llvm::dyn_cast<llvm::ArrayType>(Q.front()) )
        {
            for( unsigned i = 0; i < at->getNumContainedTypes(); i++ )
            {
                Q.push_back(at->getContainedType(i));
            }
        }
        else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(Q.front()) )
        {
            for( unsigned i = 0; i < vt->getNumContainedTypes(); i++ )
            {
                Q.push_back(vt->getContainedType(i));
            }
        }
        else
        {
            Cyclebite::Util::PrintVal(Q.front());
            throw CyclebiteException("Cannot yet handle this type when determining the size of a global constant initializer");
        }
        Q.pop_front();
    }
    return baseType;
}

template <typename T>
void recurThroughArray( T* array, const llvm::ConstantArray* a, unsigned scale, unsigned index )
{
    // we check to see what the input constant array contains
    for( unsigned i = 0; i < llvm::cast<llvm::ArrayType>(a->getType())->getNumElements(); i++ )
    {
        if( const auto& at = llvm::dyn_cast<llvm::ArrayType>(a->getAggregateElement(i)->getType()) )
        {
            // if it contains yet more arrays, we look through one more level to see if we have found the base type yet
            for( unsigned j = 0; j < at->getNumElements(); j++ )
            {
                if( a->getAggregateElement(i)->getAggregateElement(j)->getType()->isFloatTy() )
                {
                    // the base type is a float and has been found, extract it from the initializer;
                    array[scale*index + at->getNumElements()*i + j] = llvm::cast<llvm::ConstantFP>(a->getAggregateElement(i)->getAggregateElement(j))->getValueAPF().convertToFloat();
                }
                else if( const auto& ca = llvm::dyn_cast<llvm::ConstantArray>( a->getAggregateElement(i)->getAggregateElement(j) ) )
                {
                    // we haven't yet found the base type, recurse on the ConstantArray index
                    recurThroughArray<T>( array, ca, (unsigned)at->getNumElements()*scale, i);
                }
                else
                {
                    Cyclebite::Util::PrintVal(a->getAggregateElement(i)->getAggregateElement(j));
                    Cyclebite::Util::PrintVal(a->getAggregateElement(i)->getAggregateElement(j)->getType());
                    throw CyclebiteException("Don't know how to handle this type when recurring through a constant expression");
                }
            }
        }
        else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(a->getAggregateElement(i)->getType()) )
        {
            throw CyclebiteException("Cannot handle a vector type within a constant array!");
        }
        else
        {
            Cyclebite::Util::PrintVal(a->getAggregateElement(i)->getType());
            throw CyclebiteException("Cannot yet handle this type when extracting constant values from a constant array!");
        }
    }
}

template <typename T>
T* getContainedArray( const llvm::ConstantArray* a, unsigned arraySize )
{
    T* containedArray = new T[arraySize];
    // to capture multi-dimensionall arrays we need to recur through the types in the array
    recurThroughArray( containedArray, a, 1, 0);
    return containedArray;
}

void Cyclebite::Grammar::getConstant( const shared_ptr<Cyclebite::Graph::Inst>& opInst, const llvm::Constant* con, vector<shared_ptr<Symbol>>& newSymbols, map<shared_ptr<Cyclebite::Graph::DataValue>, shared_ptr<Symbol>>& nodeToExpr )
{
    // this may be loading from a constant global structure
    // in that case we are interested in finding out which value we are pulling from the structure
    // this may or may not be possible, if the indices are or aren't statically determinable
    // ex: StencilChain/Naive(BB 170)
    if( const auto& conPtr = llvm::dyn_cast<llvm::PointerType>(con->getType()) )
    {
        // we are interested in knowing if this pointer points to an array
        // to figure that out we need to find a use of this pointer that indicates what type it points tonnnnnn
        if( llvm::isa<llvm::ArrayType> ( Cyclebite::Util::getFirstContainedType(con) ) ||
            llvm::isa<llvm::VectorType>( Cyclebite::Util::getFirstContainedType(con) ) )
        {
            if( const auto& glob = llvm::dyn_cast<llvm::GlobalVariable>(con) )
            {
                if( const auto& conAg = llvm::dyn_cast<llvm::ConstantAggregate>(glob->getInitializer()) )
                {
                    if( const auto& conArray = llvm::dyn_cast<llvm::ConstantArray>(conAg) )
                    {
                        // implement a method to recur through the layers of the array
                        // - find the base type
                        // - find the permutation of entries (in this case 5 * 5)
                        unsigned arraySize = getArraySize(conArray->getType());
                        auto at = getBaseType(conArray->getType());
                        if( at->isFloatTy() )
                        {
                            float* containedArray = getContainedArray<float>(conArray, arraySize);
                            vector<shared_ptr<IndexVariable>> vars;
                            auto newSymbol = make_shared<ConstantArray<float>>(vars, containedArray, arraySize);
                            nodeToExpr[ opInst ] = newSymbol;
                            newSymbols.push_back(newSymbol); 
                            free(containedArray);
                        }
                    }
                }
            }
        } 
        else if( const auto& structTy = llvm::dyn_cast<llvm::StructType>( Cyclebite::Util::getFirstContainedType(con) ) )
        {
            throw CyclebiteException("Cannot yet support constant arrays that contain user-defined structures!");
        }
        else if( const auto& funcTy   = llvm::dyn_cast<llvm::FunctionType>( Cyclebite::Util::getFirstContainedType(con) ) )
        {
            throw CyclebiteException("Found a constant pointer used in the function group that pointed to a function!");
        }
        else if( const auto& intTy    = llvm::dyn_cast<llvm::IntegerType>( Cyclebite::Util::getFirstContainedType(con) ) )
        {
            // call the method to build a ConstantSymbol<int>

        }
    }
}