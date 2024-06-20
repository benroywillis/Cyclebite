# Cyclebite-Template
Cyclebite-Template is a task template that extracts that parallel pattern from Cyclebite task graphs.
It lifts tasks within Cyclebite task graphs (at the LLVM-IR level) to a higher level of abstraction, which facilitates transformation and optimization towards target architectures.
Cyclebite-Template exports the Cyclebite task graph to the Halide domain-specific language for transformation and optimization, which 

## Build from source
Cyclebite-Template is part of the Cyclebite repository, and is automatically built from the instructions in the main repository build flow.

## Usage
Cyclebite-Template is integrated into the build flows of the [Algorithms](https://www.github.com/benroywillis/Algorithms/tree/devb/) repository.
Thus, if you follow the instructions to integrate your project into the Algorithms repository, you can run Cyclebite-Template on your C/C++ application and exports its task graph in Halide!
The Algorithms repository uses [Halide generators](https://halide-lang.org/tutorials/tutorial_lesson_15_generators.html) to separate the exported application pipeline from its input/output methods.
More information on exporting and running your application in Halide can be found in the [README](https://www.github.com/benroywillis/Algorithms/tree/devb/README.md).
