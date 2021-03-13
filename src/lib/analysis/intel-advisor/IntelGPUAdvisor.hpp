#ifndef Analysis_Advisor_IntelGPUAdvisor_hpp
#define Analysis_Advisor_IntelGPUAdvisor_hpp

//************************* System Include Files ****************************

#include <iostream>
#include <map>
#include <queue>
#include <stack>
#include <string>
#include <tuple>
#include <vector>

//*************************** User Include Files ****************************

#include <include/uint.h>

#include <lib/binutils/LM.hpp>
#include <lib/prof/CallPath-Profile.hpp>
#include <lib/prof/Struct-Tree.hpp>

#include "IntelInstructionAnalyzer.hpp"

//*************************** Forward Declarations ***************************

//****************************************************************************

namespace Analysis {

class IntelGPUAdvisor {
 public:
  typedef std::tuple<double, Prof::CCT::ADynNode *, std::string> AdviceTuple;

 public:
  explicit IntelGPUAdvisor(Prof::CallPath::Profile *prof)
      : _prof(prof) {}

  void init(const std::string &gpu_arch);

  //void blame(CCTBlames &cct_blames);

  //void advise(const CCTBlames &cct_blames);
  
  std::vector<std::string> get_advice();

 private:
  Prof::CallPath::Profile *_prof;

  std::vector<AdviceTuple> _advice;
  std::stringstream _output;
};

}  // namespace Analysis

#endif  // Analysis_Advisor_IntelGPUAdvisor_hpp
