# Cyclebite-Template Language
Cyclebite-Template Language (CTL) is an [mlir.llvm.org](MLIR) dialect and transformation passes that target the parallel patterns extracted by Cyclebite-Template.

## Dialect
CTL contains operators that target the parallel patterns extracted by Cyclebite-Template. 
Each parallel pattern has an operator, and each operator is configurable to define its own functional expression, inputs, and outputs.

## Lowering Passes
CTL has transformation passes specific to each operator and their interconnections with other operators.
The abstraction used by the lowering passes is inspired by Jonathan Ragan-Kelley's Language of Tasks and Language of Schedules, as proposed in his dissertation when describing the Halide domain-specific language.
