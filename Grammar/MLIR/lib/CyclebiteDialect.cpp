//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "CyclebiteDialect.h"
#include "CyclebiteOps.h"

using namespace mlir;
using namespace mlir::cyclebite;

//===----------------------------------------------------------------------===//
// Cyclebite dialect.
//===----------------------------------------------------------------------===//

void CyclebiteDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "CyclebiteOps.cpp.inc"
      >();
}