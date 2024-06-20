# Cyclebite-Template
Cyclebite-Template is an application task template that lifts the abstraction level of those tasks to better facilitate transformation and optimization.
Cyclebite-Template lifts Cyclebite tasks (at the LLVM-IR level) to a higher level of abstraction by extracting their [parallel execution patterns](https://stanford-ppl.github.io/Delite/).
Cyclebite-Template exports the Cyclebite task graph to the Halide domain-specific language for transformation and optimization, which autoschedules the pipelines of the task using a [CPU scheduler](https://halide-lang.org/papers/autoscheduler2019.html) or [GPU scheduler](https://cseweb.ucsd.edu/~tzli/gpu_autoscheduler.pdf).

## Build from source
Cyclebite-Template is part of the Cyclebite repository, and is automatically built from the instructions in the main repository build flow.

## Usage
Cyclebite-Template is integrated into the build flows of the [Algorithms](https://www.github.com/benroywillis/Algorithms/tree/devb/) repository.
Thus, if you follow the instructions to integrate your project into the Algorithms repository, you can run Cyclebite-Template on your C/C++ application and exports its task graph in Halide!
The Algorithms repository uses [Halide generators](https://halide-lang.org/tutorials/tutorial_lesson_15_generators.html) to separate the exported application pipeline from its input/output methods.
More information on exporting and running your application in Halide can be found in the [README](https://www.github.com/benroywillis/Algorithms/tree/devb/README.md).
