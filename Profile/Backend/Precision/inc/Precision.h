#pragma once
#include "AtlasUtil/Exceptions.h"
#include "Epoch.h"
#include <ctime>
#include <cstdint>
#include <set>
#include <map>
#include <memory>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>

namespace TraceAtlas::Profile::Backend::Precision
{
    /// @brief Encodes a load or store when intercepting the value of a memory transaction
    enum class PrecisionMemOp
    {
        Store,
        Load
    };
    /// @brief Encodes the data type used in a memory transaction
    enum class PrecisionType
    {
        float128,
        float80,
        float64,
        float32,
        float16,
        uint64_t,
        int64_t,
        uint32_t,
        int32_t,
        uint16_t,
        int16_t,
        uint8_t,
        int8_t,
        uint1_t,
        int1_t,
        void_t,
        vector
    };
    /// @brief Structures all information gathered from a memory transaction
    struct PrecisionValue
    {
        uint32_t bb;
        uint32_t iid;
        uint32_t exp;
        PrecisionType t;
        PrecisionMemOp op;
    };
    /// @brief Holds information gathered from an application's loads or stores
    struct ValueHistogram
    {
        std::map<uint32_t, uint64_t> exp;
        void inc(uint32_t key)
        {
            exp[key]++;
        }
        uint64_t operator[](uint32_t key) const
        {
            return exp.at(key);
        }
        bool find(uint32_t key) const
        {
            return exp.find(key) != exp.end();
        }
    };
    /// Timing information
    extern struct timespec start, end;
    /// On/off switch for the profiler
    extern bool precisionActive;
    /// Holds all values found in a pass
    extern std::map<std::shared_ptr<TraceAtlas::Profile::Backend::Memory::Epoch>, ValueHistogram, TraceAtlas::Profile::Backend::Memory::UIDCompare> hist;
;
    /// Converts from llvm type to PrecisionType
    inline PrecisionType LLVMTy2PrecisionTy(llvm::Type* ty)
    {
        switch(ty->getTypeID())
        {
            // LLVM9 enums
            // all floating-point llvm type IDs are defined at https://llvm.org/doxygen/Type_8h_source.html
            case 0: 
                return PrecisionType::void_t;
            case 1: // 16-bit float with 7-bit significand
                return PrecisionType::float16;
            case 2:
                return PrecisionType::float32;
            case 3:
                return PrecisionType::float64;
            case 4: // 80-bit float (x87)
                return PrecisionType::float80;
            case 5: // 128-bit float (112-bit significand)
                return PrecisionType::float128;
            case 6: // 128-bit float (two doubles)
                return PrecisionType::float128;
            case 7: // label type
                throw AtlasException("Found a label type as an introspected value!");
            case 8: // metadata type
                throw AtlasException("Found a metadata type as an introspected value!");
            case 9: // MMX vector 64 bits
                throw AtlasException("Cannot yet support MMX 64 bit vectors!");
            case 10: 
                throw AtlasException("Found a token as an introspected value!");
            // llvm derived types
            case 11: // integer type
                {
                    auto it = llvm::cast<llvm::IntegerType>(ty);
                    uint32_t width = it->getBitWidth();
                    switch(width)
                    {
                        // LLVM IR doesn't track the sign of its integers
                        // the sign of a value is implicit in the operations it is used in ef sdiv implies its operands are signed
                        // thus we can't evaluate signed-ness without looking at an integer's uses
                        // we will punt on this for now and consider everything to be signed  
                        case 64:
                            return PrecisionType::int64_t;
                        case 32:
                            return PrecisionType::int32_t;
                        case 16:
                            return PrecisionType::int16_t;
                        case 8:
                            return PrecisionType::int8_t;
                        case 1:
                            return PrecisionType::int1_t;
                        default:
                            throw AtlasException("Cannot yet support an integer of size "+std::to_string(width));
                    }
                }
            case 12: // Function types
                //auto ft = llvm::cast<FunctionType>(ty);
                // in the case of functions, we don't care because these (to my knowledge 2023-05-03) always be pointers
                return PrecisionType::void_t;
            case 13: // struct
                // in the case of structs, there are many possible members
                // we hope that this struct will be further indexed, selecting a specific member
                // thus we make this void 
                return PrecisionType::void_t;
            case 14: // Array
                // we hope this array will be later indexed, so send back void bc it doesn't mean anything
                return PrecisionType::void_t;
            case 15: // Pointer type
                return PrecisionType::void_t;
            case 16: // SIMD vector
                {
                    auto vt = llvm::cast<llvm::VectorType>(ty);
                    return LLVMTy2PrecisionTy(vt->getElementType());
                }
            /* LLVM 15 enums
            // all floating-point llvm type IDs are defined at https://llvm.org/doxygen/Type_8h_source.html
            case 0: 
                return PrecisionType::float16;
            case 1: // 16-bit float with 7-bit significand
                return PrecisionType::float16;
            case 2:
                return PrecisionType::float32;
            case 3:
                return PrecisionType::float64;
            case 4: // 80-bit float (x87)
                return PrecisionType::float80;
            case 5: // 128-bit float (112-bit significand)
                return PrecisionType::float128;
            case 6: // 128-bit float (two doubles)
                return PrecisionType::float128;
            case 7: // void type
                return PrecisionType::void_t;
            case 8: // label type
                throw AtlasException("Found a label type as an introspected value!");
            case 9: // metadata type
                throw AtlasException("Found a metadata type as an introspected value!");
            case 10: // MMX vector 64 bits
                throw AtlasException("Cannot yet support MMX 64 bit vectors!");
            case 11:
                throw AtlasException("Cannot yet support AMX 8192 bit vectors!");
            case 12: 
                throw AtlasException("Found a token as an introspected value!");
            // llvm derived types, defined in https://llvm.org/doxygen/DerivedTypes_8h_source.html
            case 13: // integer type
                {
                    auto it = llvm::cast<llvm::IntegerType>(ty);
                    uint32_t width = it->getBitWidth();
                    switch(width)
                    {
                        // LLVM IR doesn't track the sign of its integers
                        // the sign of a value is implicit in the operations it is used in ef sdiv implies its operands are signed
                        // thus we can't evaluate signed-ness without looking at an integer's uses
                        // we will punt on this for now and consider everything to be signed  
                        case 64:
                            return PrecisionType::int64_t;
                        case 32:
                            return PrecisionType::int32_t;
                        case 16:
                            return PrecisionType::int16_t;
                        case 8:
                            return PrecisionType::int8_t;
                        case 1:
                            return PrecisionType::int1_t;
                        default:
                            throw AtlasException("Cannot yet support an integer of size "+std::to_string(width));
                    }
                }
            case 14: // Function types
                //auto ft = llvm::cast<FunctionType>(ty);
                // in the case of functions, we don't care because these (to my knowledge 2023-05-03) always be pointers
                return PrecisionType::void_t;
            case 15: // Pointer type
                return PrecisionType::void_t;
            case 16: // struct
                // in the case of structs, there are many possible members
                // we hope that this struct will be further indexed, selecting a specific member
                // thus we make this void 
                return PrecisionType::void_t;
            case 17: // Array
                // we hope this array will be later indexed, so send back void bc it doesn't mean anything
                return PrecisionType::void_t;
            case 18: // SIMD vector
                {
                    auto vt = llvm::cast<llvm::VectorType>(ty);
                    return LLVMTy2PrecisionTy(vt->getElementType());
                }
            //case 19: // scalable vector type, not in llvm9
                //auto vt = llvm::cast<
            case 19: // typed pointer used by some GPUs
                return PrecisionType::void_t;
            case 20: // TargetExtType, means nothing
                return PrecisionType::void_t;
                */
        }
    }
} // namespace TraceAtlas::Memory::Backend::Precision