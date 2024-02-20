//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ConstantArray.h"
#include "IndexVariable.h"
#include "Task.h"
#include "Export.h"
#include "Graph/inc/IO.h"
#include "Util/Helpers.h"
#include "Util/Print.h"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>

using namespace Cyclebite::Grammar;
using namespace std;

const vector<shared_ptr<IndexVariable>>& ConstantArray::getVars() const
{
    return vars;
}

ConstantType ConstantArray::getArray(void** ret) const
{
    *ret = array;
    return t;
}

int ConstantArray::getArraySize() const
{
    int arraySize = 1;
    for( auto entry : dims )
    {
        arraySize *= entry;
    }
    return arraySize;
}

string ConstantArray::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    if( !vars.empty() )
    {
        string dump = "";
        if( symbol2Symbol.contains(vars.front()) )
        {
            dump = name+"("+symbol2Symbol.at(vars.front())->dumpHalide( symbol2Symbol );
        }
        else
        {
            dump = name+"("+vars.front()->dumpHalide( symbol2Symbol );
        }
        auto varIt = next(vars.begin());
        while( varIt != vars.end() )
        {
            if( symbol2Symbol.contains( *varIt ) )
            {
                dump += ", "+symbol2Symbol.at((*varIt))->dumpHalide(symbol2Symbol);
            }
            else
            {
                dump += ", "+(*varIt)->dumpHalide(symbol2Symbol);
            }
            varIt = next(varIt);
        }
        dump += ")";
        return dump;
    }
    else
    {
        return name;
    }
}

string ConstantArray::dumpC() const
{
    string cArray = "const "+TypeToString.at(t)+" "+name+"[";
    cArray += to_string(*dims.begin())+"]";
    auto it = next(dims.begin());
    while( it != dims.end() )
    {
        cArray += "["+to_string(*it)+"]";
        it = next(it);
    }
    cArray += " = ";
    if( dims.size() == 1 )
    {
        cArray += "{ ";
        for( unsigned i = 0; i < dims[0]; i++ )
        {
            if( i > 0 )
            {
                cArray += ", ";
            }
            // the global array index is the sum of products (index*dimSize)
            switch(t)
            {
                case ConstantType::SHORT : cArray += to_string( ((short*)array)[i] ); break;
                case ConstantType::INT   : cArray += to_string( ((int*)array)[i] ); break;
                case ConstantType::FLOAT : cArray += to_string( ((float*)array)[i] )+"f"; break;
                case ConstantType::DOUBLE: cArray += to_string( ((double*)array)[i] ); break;
                case ConstantType::INT64 : cArray += to_string( ((int64_t*)array)[i] ); break;
                default                  : cArray += "";
            }
        }
        cArray += " }";
    }
    else
    {
        // this algorithm recursively implements each of the dimensions in the dims array
        // the recursion ends when a dimension has been exhausted
        // once a dimension is exhausted, we recurse back up until a non-exhausted dimension is encountered
        // the algorithm ends when all dimensions have been exhausted
        // the indices vector holds the position of each iterator
        auto indices = vector<unsigned>(dims.size(), 0);
        deque<unsigned> Q;
        // to prime the queue, push all dimension boundaries into it
        for( auto it = dims.begin(); it != dims.end(); it++ )
        {
            Q.push_front(*it);
            // we also need to initiate these dimensions in the string to
            cArray += "{ ";
            if( it == dims.begin() )
            {
                cArray += "\n\t";
            }
        }
        while( !Q.empty() )
        {
            unsigned globalOffset = 0;
            unsigned idx = 0;
            while( idx < dims.size()-(unsigned)1 )
            {
                unsigned dim_weight = 1;
                for( unsigned i = idx+1; i < dims.size(); i++ )
                {
                    dim_weight *= dims[i];
                }
                // now that we have the size coefficient of the current dimension (idx), multiply by its current index and accumulate it to the globalIndex
                globalOffset += dim_weight*indices[idx];
                idx++;
            }
            // now globalIndex points to the start of the next set of child-most entries in the array
            for( unsigned i = 0; i < Q.front(); i++ )
            {
                if( i > 0 )
                {
                    cArray += ", ";
                }
                // the global array index is the sum of products (index*dimSize)
                switch(t)
                {
                    case ConstantType::SHORT : cArray += to_string( ((short*)array)[globalOffset+i] ); break;
                    case ConstantType::INT   : cArray += to_string( ((int*)array)[globalOffset+i] ); break;
                    case ConstantType::FLOAT : cArray += to_string( ((float*)array)[globalOffset+i] )+"f"; break;
                    case ConstantType::DOUBLE: cArray += to_string( ((double*)array)[globalOffset+i] ); break;
                    case ConstantType::INT64 : cArray += to_string( ((int64_t*)array)[globalOffset+i] ); break;
                    default                  : cArray += "";
                }
            }
            cArray += " }";
            // we have exhausted this dimension, record that by bubbling up an iterator increment through all dimension levels
            int dimIndex = (int)dims.size()-2;
            while( dimIndex >= 0 )
            {
                indices[ (unsigned)dimIndex ]++;
                if( indices[(unsigned)dimIndex] == dims[(unsigned)dimIndex] )
                {
                    // we have exhausted this dimension, go to the parent
                    dimIndex--;
                    cArray += " }";
                }
                else
                {
                    // we have not yet exhausted this dimension, so don't go to the parent
                    cArray += ",\n\t";
                    break;
                }
            }
            // if dimIndex went below 0, then all dimension iterator spaces have been satisfied, we are done
            if( dimIndex < 0 )
            {
                break;
            }
            // dimIndex is now at the parent-most iterator increment, set all its child loop iterators to 0 and do it all again
            dimIndex++;
            while( dimIndex < (int)dims.size() )
            {
                Q.push_front(dims[(unsigned)dimIndex]);
                indices[(unsigned)dimIndex] = 0;
                cArray += "{ ";
                dimIndex++;
            }
        }
    }
    return cArray;
}

string getArrayType(const llvm::Constant* ptr)
{
    return Cyclebite::Util::PrintVal( Cyclebite::Util::getContainedType(ptr), false );
}

/// @brief Gets the dimensions of the constant array being analyzed
///
/// Both the size and the configuration of memory is acquired in this method
/// @param t A pointer to the llvm::ArrayType that should be analyzed
/// @param dims An empty vector that will store the dimensions of the array. Each entry in the vector is a dimension, and the value of that entry is the number of elements in that dimension
/// @return The total number of entries in the array (that is, the permutation of each entry in dims)
unsigned getArraySize(const llvm::ArrayType* t, vector<unsigned>& dims)
{
    // arrays can contain multiple dimensions of stuff, so we recur through them here, each time permuting the sizes that are found
    deque<const llvm::Type*> Q;
    set<const llvm::Type*> covered;
    Q.push_front(t);
    covered.insert(t);
    while( !Q.empty() )
    {
        if( const auto& at = llvm::dyn_cast<llvm::ArrayType>(Q.front()) )
        {
            dims.push_back((unsigned)at->getNumElements());
            for( unsigned i = 0; i < at->getNumContainedTypes(); i++ )
            {
                Q.push_back(at->getContainedType(i));
            }
        }
        else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(Q.front()) )
        {
            dims.push_back((unsigned)vt->getArrayNumElements());
            for( unsigned i = 0; i < vt->getNumContainedTypes(); i++ )
            {
                Q.push_back(vt->getContainedType(i));
            }
        }
        Q.pop_front();
    }
    // now we permute the sizes of the dimensions of the array together to get its total size
    unsigned elems = 1;
    for( unsigned i = 0; i < dims.size(); i++ )
    {
        elems *= dims[i];
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
            // this allows us to structure the types easily within the array, since the array parameters are present at the second-to-last recurrence level
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

/// @brief Returns the data contained inside a constant aggregate structure (array or vector)
///
/// This method employs an internal recursive method that dives into the types contained within the structure (if the array is multidimensional, it requires a depth search of the types)
/// @tparam T   The type of the base element that should be expected. The base element is the primitive data type contained in the structure (float, int, short). Currently, contains with multiple types are not supported. 
/// @param a            A pointer to the llvm::ConstantArray* object to be searched through
/// @param arraySize    That amount of elements that should be present in the structure
/// @return             A dynamically-allocated array containing the values in the constant. This value needs to be freed by the user. 
template <typename T>
T* getContainedArray( const llvm::ConstantArray* a, unsigned arraySize )
{
    T* containedArray = new T[arraySize];
    // to capture multi-dimensional arrays we need to recur through the types in the array
    recurThroughArray( containedArray, a, 1, 0);
    return containedArray;
}

/// @brief Finds the Cyclebite::Grammar::IndexVariables that index a global constant structure
///
/// This is inspired by the constant filter array declared in git@github.com:benroywillis/Algorithms/tree/devb/StencilChain/Naive/StencilChain.cpp
/// @retval     A vector in dimension-order of the index variables used on this global.
const vector<shared_ptr<IndexVariable>> findContainedArrayVars( const llvm::ConstantArray* a, const set<shared_ptr<IndexVariable>>& idxVars )
{
    vector<shared_ptr<IndexVariable>> vars;
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    Q.push_front(a);
    covered.insert(a);
    while( !Q.empty() )
    {
        if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
        {
            // the gep will indicate which index variables are used here
            for( const auto& idx : gep->indices() )
            {
                if( Cyclebite::Graph::DNIDMap.contains(idx) )
                {
                    for( const auto& idxVar : idxVars )
                    {
                        if( idxVar->getNode()->getInst() == idx.get() )
                        {
                            vars.push_back(idxVar);
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            for( const auto& use : Q.front()->users() )
            {
                if( !covered.contains(use) )
                {
                    Q.push_back(use);
                    covered.insert(use);
                }
            }
        }
        Q.pop_front();
    }
    return vars;
}

set<shared_ptr<ConstantSymbol>> Cyclebite::Grammar::getConstants( const shared_ptr<Task>& t, const set<shared_ptr<IndexVariable>>& idxVars )
{
    set<shared_ptr<ConstantSymbol>> cons;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& i : b->getInstructions() )
            {
                if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(i) )
                {
                    for( const auto& op : inst->getInst()->operands() )
                    {
                        if( const auto& con = llvm::dyn_cast<llvm::Constant>(op) )
                        {
                            /*if( constants.contains(con) )
                            {
                                // there already exists a constant initializer for this value, but this use case has its own idxVars
                                // so we need to make a copy of this global with the same name as the existing one
                                cons.insert( constants.at(con) );
                                break;
                            }*/
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
                                                vector<unsigned> dims;
                                                unsigned arraySize = getArraySize(conArray->getType(), dims);
                                                auto at = getBaseType(conArray->getType());
                                                if( at->isFloatTy() )
                                                {
                                                    float* containedArray = getContainedArray<float>(conArray, arraySize);
                                                    auto vars = findContainedArrayVars(conArray, idxVars);
                                                    auto newCon = make_shared<ConstantArray>(con, vars, containedArray, ConstantType::FLOAT, dims);
                                                    constants[con].insert( newCon );
                                                    cons.insert(newCon);
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
                                    throw CyclebiteException("Cannot yet support building constant integer aggregates!");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return cons;
}