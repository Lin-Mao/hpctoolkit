#ifndef INTEL_GPU_INSTRUCTION_H
#define INTEL_GPU_INSTRUCTION_H

#include <vector>

#include "IntelGPUAdvisor.hpp"

//std::vector<Analysis::IntelGPUAdvisor::AdviceTuple>
std::vector<std::string>
overlayIntelGPUInstructionsMain
(
 Prof::CallPath::Profile &prof,
 const std::vector<std::string> &instruction_files,
 const std::string &gpu_arch
);

#endif  // INTEL_GPU_INSTRUCTION_H
