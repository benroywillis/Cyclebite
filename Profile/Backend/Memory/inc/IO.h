#pragma once
#include <string>
namespace TraceAtlas::Profile::Backend::Memory
{
    void ReadKernelFile();
    std::string GenerateInstanceDot();
    std::string GenerateTaskOnlyInstanceDot();
    void GenerateTaskGraph();
    void GenerateTaskOnlyTaskGraph();
    void OutputKernelInstances();

} // namespace TraceAtlas::Profile::Backend::Memory