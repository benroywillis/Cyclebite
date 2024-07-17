# Cyclebite-Template Dialect
Cyclebite-Template Language (CTL) is an mlir dialect that targets the parallel patterns extracted by Cyclebite.

## Dialect
CTL contains operators that target the parallel patterns extracted by Cyclebite-Template. 

## Lowering Passes
CTL has transformation passes specific to each operator and their interconnections with other operators.
The abstraction used by the lowering passes is inspired by Jonathan Ragan-Kelley's Language of Tasks and Language of Schedules, as proposed in his dissertation when describing the Halide domain-specific language.
