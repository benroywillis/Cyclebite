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

def Cyclebite : Dialect {
    let name = "cyclebite";
    let summary = "Targeted by Cyclebite-Template's export tool. Contains an operation for each label it extracts from Cyclebite tasks.";
    let description = [{
        Cyclebite-Template extracts canonical expressions for each task in the task graphs exported by Cyclebite.
        A label for each task is inferred from its canonical expressions - these labels show the parallel pattern of the task.
        Currently, the only supported parallel pattern label that is supported by this IR is ForEach.

        For each supported label, an operation is defined for it.
        This operation is inspired by the Linalg dialect's generic operator.
        The expression defined within the operation is the functional transformation from the canonical expression.
        The parallel pattern implies how this expression (the "atomic expression") is interconnected with all other atomic expressions in the operation.
        Additionally, some parameters are included with the operation to show what its iterator space is (i.e., how much work there is to do.)
    }];
    let cppNamespace = "::mlir::cyclebite";
}

//===----------------------------------------------------------------------===//
// Base cyclebite operation definition.
//===----------------------------------------------------------------------===//

class Cyclebite_Op<string mnemonic, list<Trait> traits = []> :
        Op<Cyclebite_Dialect, mnemonic, traits>;

#endif // CYCLEBITE_DIALECT