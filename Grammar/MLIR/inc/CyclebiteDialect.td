//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//

#ifndef CYCLEBITE_DIALECT
#define CYCLEBITE_DIALECT

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Cyclebite dialect definition.
//===----------------------------------------------------------------------===//

def Cyclebite_Dialect : Dialect {
    let name = "cyclebite";
    let summary = "A cyclebite out-of-tree MLIR dialect.";
    let description = [{
        This dialect is an example of an out-of-tree MLIR dialect designed to
        illustrate the basic setup required to develop MLIR-based tools without
        working inside of the LLVM source tree.
    }];
    let cppNamespace = "::mlir::cyclebite";
}

//===----------------------------------------------------------------------===//
// Base cyclebite operation definition.
//===----------------------------------------------------------------------===//

class Cyclebite_Op<string mnemonic, list<Trait> traits = []> :
        Op<Cyclebite_Dialect, mnemonic, traits>;

#endif // CYCLEBITE_DIALECT