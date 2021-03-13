#ifndef _INTEL_ANALYZE_INSTRUCTION_H_
#define _INTEL_ANALYZE_INSTRUCTION_H_

//******************************************************************************
// system includes
//******************************************************************************

#include <vector>



//******************************************************************************
// local includes
//******************************************************************************

#include <CodeObject.h>
#include "DotCFG.hpp"     // GPUParse



//******************************************************************************
// interface operations
//******************************************************************************

void
sliceIntelInstructions
(
 const Dyninst::ParseAPI::CodeObject::funclist &func_set,
 std::vector<GPUParse::Function *> functions
);


void
readIntelInstructions
(
 std::string file,
 std::vector<GPUParse::Function *> functions
);

#endif // _INTEL_ANALYZE_INSTRUCTION_H_

