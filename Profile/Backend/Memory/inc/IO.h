#pragma once
#include <string>
namespace Cyclebite::Profile::Backend::Memory
{
    void ReadKernelFile();
    std::string GenerateInstanceDot();
    std::string GenerateTaskOnlyInstanceDot();
    void GenerateTaskGraph();
    void GenerateTaskOnlyTaskGraph();
    void OutputKernelInstances();
} // namespace Cyclebite::Profile::Backend::Memory