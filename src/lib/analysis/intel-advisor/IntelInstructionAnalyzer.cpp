//******************************************************************************
// system includes
//******************************************************************************

#include <AbslocInterface.h>
#include <Graph.h>
#include <slicing.h>



//******************************************************************************
// local includes
//******************************************************************************

//#include "DotCFG.hpp"
#include "IntelInstructionAnalyzer.hpp"



//******************************************************************************
// macros
//******************************************************************************

#define INSTRUCTION_ANALYZER_DEBUG 0



//******************************************************************************
// local data
//******************************************************************************

static int TRACK_LIMIT = 8;



//******************************************************************************
// type definitions
//******************************************************************************

#if 0
class IgnoreRegPred : public Dyninst::Slicer::Predicates {
  public:
    IgnoreRegPred(std::vector<Dyninst::AbsRegion> &rhs) : _rhs(rhs) {}

    virtual bool modifyCurrentFrame(Dyninst::Slicer::SliceFrame &slice_frame,
        Dyninst::GraphPtr graph_ptr, Dyninst::Slicer *slicer) {
      std::vector<Dyninst::AbsRegion> delete_abs_regions;

      for (auto &active_iter : slice_frame.active) {
        // Filter unmatched regs
        auto &abs_region = active_iter.first;
        bool find = false;
        for (auto &rhs_abs_region : _rhs) {
          if (abs_region.absloc().reg() == rhs_abs_region.absloc().reg()) {
            find = true;
            break;
          }
        }
        if (find == false) {
          delete_abs_regions.push_back(abs_region);
        }
      }

      for (auto &abs_region : delete_abs_regions) {
        slice_frame.active.erase(abs_region);
      }

      return true;
    }

  private:
    std::vector<Dyninst::AbsRegion> _rhs;
};



//******************************************************************************
// private operations
//******************************************************************************

static void trackDependency(const std::map<int, GPUParse::InstructionStat *> &inst_stat_map,
    Dyninst::Address inst_addr, Dyninst::Address func_addr,
    std::map<int, int> &predicate_map, Dyninst::NodeIterator exit_node_iter,
    GPUParse::InstructionStat *inst_stat, int barriers, int step) {
  if (step >= TRACK_LIMIT) {
    return;
  }
  Dyninst::NodeIterator in_begin, in_end;
  (*exit_node_iter)->ins(in_begin, in_end);
  for (; in_begin != in_end; ++in_begin) {
    auto slice_node = boost::dynamic_pointer_cast<Dyninst::SliceNode>(*in_begin);
    auto addr = slice_node->addr();
    auto *slice_inst = inst_stat_map.at(addr);

    if (INSTRUCTION_ANALYZER_DEBUG) {
      std::cout << "find inst_addr " << inst_addr - func_addr << " <- addr: " << addr - func_addr;
    }    

    Dyninst::Assignment::Ptr aptr = slice_node->assign();
    auto reg = aptr->out().absloc().reg();
    auto reg_id = reg.val() & 0xFF;
    if (reg.val() & Dyninst::intel::PR) {
      if (reg_id == inst_stat->predicate) {
        auto beg = inst_stat->predicate_assign_pcs.begin();
        auto end = inst_stat->predicate_assign_pcs.end();
        if (std::find(beg, end, addr - func_addr) == end) {
          inst_stat->predicate_assign_pcs.push_back(addr - func_addr);
        }    
      }    

      for (size_t i = 0; i < inst_stat->psrcs.size(); ++i) {
        if (reg_id == inst_stat->psrcs[i]) {
          auto beg = inst_stat->passign_pcs[reg_id].begin();
          auto end = inst_stat->passign_pcs[reg_id].end();
          if (std::find(beg, end, addr - func_addr) == end) {
            inst_stat->passign_pcs[reg_id].push_back(addr - func_addr);
          }
          break;
        }
      }

      if (INSTRUCTION_ANALYZER_DEBUG) {
        std::cout << " predicate reg " << reg_id << std::endl;
      }
    } else if (reg.val() & Dyninst::intel::BR) {
      for (size_t i = 0; i < inst_stat->bsrcs.size(); ++i) {
        if (reg_id == inst_stat->bsrcs[i]) {
          auto beg = inst_stat->bassign_pcs[reg_id].begin();
          auto end = inst_stat->bassign_pcs[reg_id].end();
          if (std::find(beg, end, addr - func_addr) == end) {
            inst_stat->bassign_pcs[reg_id].push_back(addr - func_addr);
          }
          break;
        }
      }

      if (INSTRUCTION_ANALYZER_DEBUG) {
        std::cout << " barrier " << reg_id << std::endl;
      }
    } else if (reg.val() & Dyninst::intel::ARF) {
      for (size_t i = 0; i < inst_stat->arfsrcs.size(); ++i) {
        if (reg_id == inst_stat->arfsrcs[i]) {
          auto beg = inst_stat->uassign_pcs[reg_id].begin();
          auto end = inst_stat->uassign_pcs[reg_id].end();
          if (std::find(beg, end, addr - func_addr) == end) {
            inst_stat->uassign_pcs[reg_id].push_back(addr - func_addr);
          }
          break;
        }
      }

      if (INSTRUCTION_ANALYZER_DEBUG) {
        std::cout << " uniform " << reg_id << std::endl;
      }    
    } else {
      for (size_t i = 0; i < inst_stat->srcs.size(); ++i) {
        if (reg_id == inst_stat->srcs[i]) {
          auto beg = inst_stat->assign_pcs[reg_id].begin();
          auto end = inst_stat->assign_pcs[reg_id].end();
          if (std::find(beg, end, addr - func_addr) == end) {
            inst_stat->assign_pcs[reg_id].push_back(addr - func_addr);
          }
          break;
        }
      }

      if (INSTRUCTION_ANALYZER_DEBUG) {
        std::cout << " reg " << reg_id << std::endl;
      }
    }

    if (slice_inst->predicate_flag == GPUParse::InstructionStat::PREDICATE_NONE && barriers == -1) {
      // 1. No predicate, stop immediately
    } else if (inst_stat->predicate == slice_inst->predicate &&
        inst_stat->predicate_flag == slice_inst->predicate_flag && barriers == -1) {
      // 2. Find an exact match, stop immediately
    } else {
      if (((slice_inst->predicate_flag == GPUParse::InstructionStat::PREDICATE_TRUE &&
              predicate_map[-(slice_inst->predicate + 1)] > 0) ||
            (slice_inst->predicate_flag == GPUParse::InstructionStat::PREDICATE_FALSE &&
             predicate_map[(slice_inst->predicate + 1)] > 0)) && barriers == -1) {
        // 3. Stop if find both !@PI and @PI=
        // add one to avoid P0
      } else {
        // 4. Continue search
        if (slice_inst->predicate_flag == GPUParse::InstructionStat::PREDICATE_TRUE) {
          predicate_map[slice_inst->predicate + 1]++;
        } else {
          predicate_map[-(slice_inst->predicate + 1)]++;
        }

        trackDependency(inst_stat_map, inst_addr, func_addr, predicate_map, in_begin, inst_stat,
            barriers, step + 1);

        // Clear
        if (slice_inst->predicate_flag == GPUParse::InstructionStat::PREDICATE_TRUE) {
          predicate_map[slice_inst->predicate + 1]--;
        } else {
          predicate_map[-(slice_inst->predicate + 1)]--;
        }
      }
    }    
  }
}
#endif


//******************************************************************************
// interface operations
//******************************************************************************

void
readIntelInstructions
(
 std::string file,
 std::vector<GPUParse::Function *> functions
)
{
  return;
}


void
sliceIntelInstructions
(
 const Dyninst::ParseAPI::CodeObject::funclist &func_set,
 std::vector<GPUParse::Function *> functions
)
{
  // Build a instruction map
  std::map<int, GPUParse::InstructionStat *> inst_stat_map;
  std::map<int, GPUParse::Block *> inst_block_map;
  for (auto *function : functions) {
    for (auto *block : function->blocks) {
      for (auto *inst : block->insts) {
        if (inst->inst_stat) {
          auto *inst_stat = inst->inst_stat;
          inst_stat_map[inst->offset] = inst_stat;
          inst_block_map[inst->offset] = block;
        }
      }
    }
  }

  std::vector<std::pair<Dyninst::ParseAPI::Block *, Dyninst::ParseAPI::Function *>> block_vec;
  for (auto dyn_func : func_set) {
    for (auto *dyn_block : dyn_func->blocks()) {
      block_vec.emplace_back(dyn_block, dyn_func);
    }
  }

  Dyninst::AssignmentConverter ac(true, false);
  Dyninst::Slicer::InsnCache dyn_inst_cache;

  for (size_t i = 0; i < block_vec.size(); ++i) {
    auto *dyn_block = block_vec[i].first;
    auto *dyn_func = block_vec[i].second;
    auto func_addr = dyn_func->addr();

    Dyninst::ParseAPI::Block::Insns insns;
    dyn_block->getInsns(insns);

    for (auto &inst_iter : insns) {
      auto &inst = inst_iter.second;
      auto inst_addr = inst_iter.first;
      auto *inst_stat = inst_stat_map.at(inst_addr);

      if (INSTRUCTION_ANALYZER_DEBUG) {
        std::cout << "try to find inst_addr " << inst_addr - func_addr << std::endl;
      }

      std::vector<Dyninst::Assignment::Ptr> assignments;
      ac.convert(inst, inst_addr, dyn_func, dyn_block, assignments);

      for (auto a : assignments) {
#ifdef FAST_SLICING
        FirstMatchPred p;
#else
        // commented till I understand why a register predicate needs to be ignored
        IgnoreRegPred p(a->inputs());
#endif

        Dyninst::Slicer s(a, dyn_block, dyn_func, &ac, &dyn_inst_cache);
        Dyninst::GraphPtr g = s.backwardSlice(p);

        Dyninst::NodeIterator exit_begin, exit_end;
        g->exitNodes(exit_begin, exit_end);

        for (; exit_begin != exit_end; ++exit_begin) {
          std::map<int, int> predicate_map;
          // DFS to iterate the whole dependency graph
          if (inst_stat->predicate_flag == GPUParse::InstructionStat::PREDICATE_TRUE) {
            predicate_map[inst_stat->predicate + 1]++;
          } else if (inst_stat->predicate_flag == GPUParse::InstructionStat::PREDICATE_FALSE) {
            predicate_map[-(inst_stat->predicate + 1)]++;
          }
#ifdef FAST_SLICING
          TRACK_LIMIT = 1;
#endif
          auto barrier_threshold = inst_stat->barrier_threshold;
          //trackDependency(inst_stat_map, inst_addr, func_addr, predicate_map, exit_begin, inst_stat,
          //    barrier_threshold, 0);
        }
      }
    }
  }
}

