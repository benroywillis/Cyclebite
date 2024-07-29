//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//

#ifndef CYCLEBITE_OPS
#define CYCLEBITE_OPS

include "CyclebiteDialect.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def ForEach : Cyclebite_Op<"ForEach", [DeclareOpInterfaceMethods<OpAsmOpInterface, ["getAsmBlockArgumentNames"]>, AttrSizedOperandSegments]> {
    let summary = "Represents a ForEach task, where the iterator pattern and atomic transformation expression are defined as inputs, and each atomic expression is interconnected as a ForEach parallel pattern.";
    let description = [{
        Example:

        ```mlir
        cyclebite.ForEach #trait_attribute
          ins(%A, %B : memref<?x?xf32, stride_specification>,
                       memref<?x?xf32, stride_specification>)
          outs(%C : memref<?x?xf32, stride_specification>)
          attrs = {other-optional-attributes}
          {region}
        ```
    Where #trait_attributes is an alias of a dictionary attribute containing:
      - doc [optional]: a documentation string
      - indexing_maps: a list of AffineMapAttr, one AffineMapAttr per each input
        and output view. Such AffineMapAttr specifies the mapping between the
        loops and the indexing within each view.
      - library_call [optional]: a StringAttr containing the name of an
        external library function that the cyclebite.ForEach operation maps to.
        The external library is assumed to be dynamically linked and no strong
        compile-time guarantees are provided. In the absence of such a library
        call, cyclebite.ForEach will always lower to loops.
      - iterator_types: an ArrayAttr specifying the type of the enclosing loops.
        Each element of the list represents and iterator of one of the following
        types:
          parallel, reduction, window
    Example:
    Defining a #brighten_trait attribute in MLIR can be done as follows:
      ```mlir
      #brighten_accesses = [
        (y, x) -> (y, x),
      ]
      #brighten_trait = {
        doc = "I(y, x) = scale*I(y, x)",
        indexing_maps = #brighten_accesses,
        library_call = "cyclebite_foreach",
        iterator_types = ["parallel", "parallel"]
      }
      ```

    And can be reused in multiple places as:
      ```mlir
      cyclebite.ForEach #brighten_trait
        ins(%I : memref<?x?xf32, stride_specification>,
            %s : f32)
        {other-optional-attributes} {
        ^bb0(%i: f32, %s: f32) :
          %i = arith.mulf %i, %s: f32
          Cyclebite.yield %i : f32
        }
      ```
    }];

    let arguments = (ins Variadic<AnyType>:$inputs,
                         Variadic<AnyShaped>:$outputs,
                         AffineMapArrayAttr:$indexing_maps,
                         IteratorTypeArrayAttr:$iterator_types,
                         OptionalAttr<StrAttr>:$doc,
                         OptionalAttr<StrAttr>:$library_call);
    let results = (outs Variadic<AnyRankedTensor>:$result_tensors);

    let builders = [
        OpBuilder<(ins "TypeRange":$resultTensorTypes, "ValueRange":$inputs,
        "ValueRange":$outputs, "ArrayAttr":$indexingMaps,
        "ArrayAttr":$iteratorTypes, "StringAttr":$doc,
        "StringAttr":$libraryCall,
        "function_ref<void(OpBuilder &, Location, ValueRange)>",
        CArg<"ArrayRef<NamedAttribute>", "{}">:$attributes)>,
    ];

    let extraClassDeclaration = structuredOpsBaseDecls # [{
        SmallVector<StringRef, 8> cyclebiteTraitAttrNames() {
            return SmallVector<StringRef, 8>{
                getDocAttrName(),
                getIndexingMapsAttrName(), getLibraryCallAttrName(),
                getIteratorTypesAttrName(),
            };
        }
        std::string getLibraryCallName() {
            return getLibraryCall() ? getLibraryCall()->str() : "op_has_no_registered_library_name";
        }

        static std::function<void(ImplicitLocOpBuilder &, Block &, ArrayRef<NamedAttribute>)> getRegionBuilder() {
            return nullptr;
        }

        MutableOperandRange getDpsInitsMutable() { return getOutputsMutable(); }
    }];

    let hasCanonicalizer = 1;
    let hasCustomAssemblyFormat = 1;
    let hasFolder = 1;
    let hasVerifier = 1;
}

#endif // CYCLEBITE_OPS