#ifndef BANAL_GPU_GPU_BLOCK_H
#define BANAL_GPU_GPU_BLOCK_H

#include <CFG.h>
#include "DotCFG.hpp"   // GPUParse

namespace Dyninst {
namespace ParseAPI {

class PARSER_EXPORT GPUBlock : public Block {
 public:
  GPUBlock(CodeObject * o, CodeRegion * r, Address start,
    std::vector<GPUParse::Inst *> insts, Architecture arch);

  virtual ~GPUBlock() {}

  virtual void getInsns(Insns &insns) const;

  virtual Address last() const;

 private:
  std::vector<GPUParse::Inst *> _insts;
  Architecture _arch;
};

}
}

#endif
