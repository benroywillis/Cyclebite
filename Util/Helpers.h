#include <llvm/IR/Instruction.h>
#include <llvm/IR/TypedPointerType.h>
#include "Util/Exceptions.h"

namespace Cyclebite::Util
{
    /// @brief This method return the the first type not found to be a pointer 
    /// 
    /// For example, if the input argument is a pointer that points to an array of doubles, this method return a const llvm::ArrayType*
    /// Or, if the input argument is a pointer to a user-defined struct, this methods return a const llvm::StructType*
    /// If the input value is not a pointer, its type is returned (whatever that happens to be)
    /// @param 
    /// @retval A constant pointer to the relevant llvm type
    inline const llvm::Type* getFirstContainedType( const llvm::Value* val, const llvm::Value** foundValue = nullptr )
    {
        // llvm no longer supports contained types in their class definition - types are inferred from the instructions
        // thus, to find the contained primitive type of this base pointer, we have to walk the DFG looking for geps
        // - when we find a load, it will extract a certain type from the pointer - this gives us our answer
        //   -- corner case: sometimes the load is a byte array that is casted to something else before it is used - we want the type from that case, not the loaded type
        std::deque<const llvm::Value*> Q;
        std::set<const llvm::Value*> covered;
        Q.push_front(val);
        covered.insert(val);
        while( !Q.empty() )
        {
            // check the type of the gep first, this will indicate the container type pointed to, if any
            if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
            {
                if( !llvm::isa<llvm::PointerType>(gep->getSourceElementType()) && !llvm::isa<llvm::TypedPointerType>(gep->getSourceElementType()) )
                {
                    if( foundValue )
                    {
                        *foundValue = gep;
                    }
                    return gep->getSourceElementType();
                }
                else
                {
                    for( const auto& use : gep->users() )
                    {
                        if( !covered.contains(use) )
                        {
                            Q.push_back(use);
                            covered.insert(use);
                        }
                    }
                }
            }
            else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
            {
                // check the returned type of the load
                if( !llvm::isa<llvm::PointerType>(ld->getType()) && !llvm::isa<llvm::TypedPointerType>(ld->getType()) )
                {
                    if( foundValue )
                    {
                        *foundValue = gep;
                    }
                    return ld->getType();
                }
                else
                {
                    for( const auto& use : ld->users() )
                    {
                        if( !covered.contains(use) )
                        {
                            Q.push_back(use);
                            covered.insert(use);
                        }
                    }
                }
            }
            else if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
            {
                // base pointers can be put into local allocations
                // thus, if the base pointer is the value operand in this store, we need to follow the pointer now
                if( st->getValueOperand() == Q.front() )
                {
                    if( !covered.contains(st->getPointerOperand()) )
                    {
                        Q.push_back(st->getPointerOperand());
                        covered.insert(st->getPointerOperand());
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
        if( foundValue )
        {
            *foundValue = val;
        }
        return val->getType();
    }

    /// @brief This method returns the primitive type contained by the pointer
    ///
    /// For example, if the pointer points to an array of floats, this method returns a const llvm::FloatTy*
    /// Or, if this array contains a vector of doubles, this method returns a const llvm::DoubleTy*
    /// If the input value is not a pointer, its type is returned (whatever that happens to be)
    /// @param val 
    /// @return 
    inline const llvm::Type* getContainedType(const llvm::Value* val)
    {
        const llvm::Value* foundValue = nullptr;
        const auto foundType = getFirstContainedType(val, &foundValue);
        if( const auto& ar = llvm::dyn_cast<llvm::ArrayType>(foundType) )
        {
            if( foundValue != val )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(foundValue) )
                {
                    for( const auto& user : gep->users() )
                    {
                        return getContainedType(user);
                    }
                }
                else
                {
                    return getContainedType(foundValue);
                }
            }
        }
        else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(foundType) )
        {
            if( foundValue != val )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(foundValue) )
                {
                    for( const auto& user : gep->users() )
                    {
                        return getContainedType(user);
                    }
                }
                else
                {
                    return getContainedType(foundValue);
                }
            }
        }
        return foundType;
    }
} // namespace Cyclebite::Util