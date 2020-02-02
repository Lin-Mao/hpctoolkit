//************************* System Include Files ****************************

#include <iostream>
#include <fstream>

#include <string>
#include <climits>
#include <cstring>
#include <cstdio>

#include <typeinfo>
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <limits>

#include <sys/stat.h>

//*************************** User Include Files ****************************

#include <include/uint.h>
#include <include/gcc-attr.h>
#include <include/gpu-metric-names.h>

#include "CallPath-CudaAdvisor.hpp"
#include "MetricNameProfMap.hpp"
#include "CCTGraph.hpp"

using std::string;

#include <lib/prof/CCT-Tree.hpp>
#include <lib/prof/Struct-Tree.hpp>
#include <lib/prof/Metric-Mgr.hpp>
#include <lib/prof/Metric-ADesc.hpp>

#include <lib/profxml/XercesUtil.hpp>
#include <lib/profxml/PGMReader.hpp>

#include <lib/prof-lean/hpcrun-metric.h>

#include <lib/binutils/LM.hpp>
#include <lib/binutils/VMAInterval.hpp>

#include <lib/cuda/DotCFG.hpp>

#include <lib/xml/xml.hpp>

#include <lib/support/diagnostics.h>
#include <lib/support/Logic.hpp>
#include <lib/support/IOUtil.hpp>
#include <lib/support/StrUtil.hpp>

#define DEBUG_CALLPATH_CUDAADVISOR 1

namespace Analysis {

namespace CallPath {

/*
 * Debug methods
 */

void CudaAdvisor::debugCCTDepGraph(CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  std::cout << "Nodes (" << cct_dep_graph.size() << "):" << std::endl;
  for (auto it = cct_dep_graph.nodeBegin(); it != cct_dep_graph.nodeEnd(); ++it) {
    auto *node = *it;
    auto node_vma = node->lmIP();

    std::cout << std::hex << "0x" << node_vma << std::dec << std::endl;
  }

  std::cout << "Edges:" << std::endl;
  for (auto it = cct_dep_graph.edgeBegin(); it != cct_dep_graph.edgeEnd(); ++it) {
    auto *from_node = it->from;
    auto *to_node = it->to;

    auto from_vma = from_node->lmIP();
    auto to_vma = to_node->lmIP();

    std::cout << std::hex << "0x" << from_vma << "-> 0x" << to_vma << std::dec << std::endl;
  }

  std::cout << "Outstanding latencies:" << std::endl;
  for (auto mpi_rank = 0; mpi_rank < _metric_name_prof_map->num_mpi_ranks(); ++mpi_rank) {
    for (auto thread_id = 0; thread_id < _metric_name_prof_map->num_thread_ids(mpi_rank); ++thread_id) {
      // Skip tracing threads
      if (_metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric) == -1) {
        continue;
      }

      // <dep_stalls, <vmas> >
      std::map<int, std::vector<int> > exec_dep_vmas;
      std::map<int, std::vector<int> > mem_dep_vmas;

      for (auto it = cct_dep_graph.nodeBegin(); it != cct_dep_graph.nodeEnd(); ++it) {
        auto *node = *it;
        auto node_vma = node->lmIP();
        auto exec_dep_stall_metric_id = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _exec_dep_stall_metric);
        auto exec_dep_stall_metric = node->demandMetric(exec_dep_stall_metric_id);
        auto mem_dep_stall_metric_id = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _mem_dep_stall_metric);
        auto mem_dep_stall_metric = node->demandMetric(mem_dep_stall_metric_id);

        exec_dep_vmas[exec_dep_stall_metric].push_back(node_vma);
        mem_dep_vmas[mem_dep_stall_metric].push_back(node_vma);
      }

      std::cout << "[ " << mpi_rank << ", " << thread_id << "]" << std::endl;

      std::cout << "exec_deps" << std::endl;
      for (auto &iter : exec_dep_vmas) {
        std::cout << iter.first << ": ";
        for (auto vma : iter.second) {
          std::cout << std::hex << "0x" << vma << std::dec << ", ";
        }
        std::cout << std::endl;
      }

      std::cout << "mem_deps" << std::endl;
      for (auto &iter : mem_dep_vmas) {
        std::cout << iter.first << ": ";
        for (auto vma : iter.second) {
          std::cout << std::hex << "0x" << vma << std::dec << ", ";
        }
        std::cout << std::endl;
      }
    }
  }
}


void CudaAdvisor::debugInstBlames(InstBlames &inst_blames) {
  for (auto &inst_blame : inst_blames) {
    std::cout << std::hex << inst_blame.dst->pc << "->";
    if (inst_blame.src->pc != inst_blame.dst->pc) {
      std::cout << inst_blame.src->pc << ", ";
    } else {
      std::cout << "-1, ";
    }
    std::cout << std::dec << _metric_name_prof_map->name(inst_blame.metric_id) << ": " <<
      inst_blame.value << std::endl;
  }
}


/*
 * Private methods
 */

void CudaAdvisor::constructVMAProfMap(VMAProfMap &vma_prof_map) {
  auto *prof_root = _gpu_root;
  Prof::CCT::ANodeIterator prof_it(prof_root, NULL/*filter*/, true/*leavesOnly*/,
    IteratorStack::PreOrder);
  for (Prof::CCT::ANode *n = NULL; (n = prof_it.current()); ++prof_it) {
    Prof::CCT::ADynNode* n_dyn = dynamic_cast<Prof::CCT::ADynNode*>(n);
    if (n_dyn) {
      VMA n_lm_ip = n_dyn->lmIP();
      vma_prof_map[n_lm_ip] = n_dyn;
    }
  }
}


void CudaAdvisor::constructVMAStructMap(VMAStructMap &vma_struct_map) {
  auto *struct_root = _prof->structure()->root();
  Prof::Struct::ANodeIterator struct_it(struct_root, NULL/*filter*/, true/*leavesOnly*/,
    IteratorStack::PreOrder);
  for (Prof::Struct::ANode *n = NULL; (n = struct_it.current()); ++struct_it) {
    if (n->type() == Prof::Struct::ANode::TyStmt) {
      auto *stmt = dynamic_cast<Prof::Struct::Stmt *>(n);
      for (auto &vma_interval : stmt->vmaSet()) {
        vma_struct_map[vma_interval] = n;
      }
    }
  }
}


void CudaAdvisor::constructVMAInstMap(const std::vector<CudaParse::Function *> &functions,
  VMAInstMap &vma_inst_map) {
  for (auto *function : functions) {
    for (auto *block : function->blocks) {
      for (auto *inst : block->insts) {
        auto vma = inst->inst_stat->pc;
        vma_inst_map[vma] = inst->inst_stat;
      }
    }
  }
}


int CudaAdvisor::demandNodeMetric(int mpi_rank, int thread_id, Prof::CCT::ADynNode *node) {
  auto in_issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric, true);
  auto ex_issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric, false);
  auto in_inst_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric, true);
  auto ex_inst_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric, false);

  // Skip tracing threads
  if (ex_inst_metric_index == -1) {
    return 0;
  }

  int ret = node->demandMetric(in_issue_metric_index);
  if (ret == 0) {
    node->demandMetric(in_issue_metric_index) += 1;
    node->demandMetric(ex_issue_metric_index) += 1;
    node->demandMetric(in_inst_metric_index) += 1;
    node->demandMetric(ex_inst_metric_index) += 1;
    ret = 1;
  }
  return ret;
}


void CudaAdvisor::initInstDepGraph(const std::vector<CudaParse::Function *> &functions,
  const VMAInstMap &vma_inst_map, CCTGraph<CudaParse::InstructionStat *> &inst_dep_graph) {
  std::map<CudaParse::Block *, std::vector<CudaParse::Block *> > dep_blocks;
  // Construct block dependencies
  for (auto *function : functions) {
    for (auto *block : function->blocks) {
      for (auto *target : block->targets) {
        dep_blocks[target->block].push_back(block);
      }
    }
  }

  std::set<std::pair<CudaParse::InstructionStat *, CudaParse::InstructionStat *> > inst_issue_dep_pairs;
  std::set<std::pair<CudaParse::InstructionStat *, CudaParse::InstructionStat *> > inst_latency_dep_pairs;

  for (auto *function : functions) {
    for (auto *block : function->blocks) {
      for (size_t i = 0; i < block->insts.size(); ++i) {
        auto *inst = block->insts[i]->inst_stat;
        inst_dep_graph.addNode(inst);
        // Add issue dependencies
        if (i == 0) {
          // First instruction
          for (auto *dep_block : dep_blocks[block]) {
            auto *dep_inst = dep_block->insts.back()->inst_stat;
            inst_dep_graph.addEdge(inst, dep_inst);
            inst_issue_dep_pairs.insert(std::pair<CudaParse::InstructionStat *,
              CudaParse::InstructionStat *>(inst, dep_inst));
          }
        } else {
          auto *dep_inst = block->insts[i - 1]->inst_stat;
          inst_dep_graph.addEdge(inst, dep_inst);
          inst_issue_dep_pairs.insert(std::pair<CudaParse::InstructionStat *,
            CudaParse::InstructionStat *>(inst, dep_inst));
        }
        // Add latency dependencies
        for (auto &iter : inst->assign_pcs) {
          for (auto pc : iter.second) {
            auto *dep_inst = vma_inst_map.at(pc);
            inst_dep_graph.addEdge(inst, dep_inst);
            inst_latency_dep_pairs.insert(std::pair<CudaParse::InstructionStat *,
              CudaParse::InstructionStat *>(inst, dep_inst));
          }
        }
      }
    }
  }
}


void CudaAdvisor::propagateCCTGraph(const VMAInstMap &vma_inst_map,
  CCTGraph<CudaParse::InstructionStat *> &inst_dep_graph, VMAProfMap &vma_prof_map, 
  int mpi_rank, int thread_id, CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  // Init queue
  std::queue<Prof::CCT::ADynNode *> nodes;
  for (auto iter = cct_dep_graph.nodeBegin(); iter != cct_dep_graph.nodeEnd(); ++iter) {
    auto *node = *iter;
    // Every stalled instruction must be issued at least once
    demandNodeMetric(mpi_rank, thread_id, node);
    nodes.push(node);
  }

  // Complexity O(N)
  while (nodes.empty() == false) {
    auto *node = nodes.front();
    nodes.pop();
    auto *parent = node->parent();

    auto node_vma = node->lmIP();
    // Must have a correponding instruction
    auto *node_inst = vma_inst_map.at(node_vma);
    auto inst_iter = inst_dep_graph.outgoing_nodes(node_inst);

    if (inst_iter != inst_dep_graph.outgoing_nodes_end()) {
      for (auto *inst_stat : inst_iter->second) {
        auto vma = inst_stat->pc;
        auto iter = vma_prof_map.find(vma);
        if (iter != vma_prof_map.end()) {
          // Existed CCT node
          auto *neighbor_node = iter->second;
          cct_dep_graph.addEdge(node, neighbor_node);
        } else {
          Prof::Metric::IData metric_data(_prof->metricMgr()->size());
          metric_data.clearMetrics();
          auto *neighbor_node = new Prof::CCT::Stmt(parent,
            HPCRUN_FMT_CCTNodeId_NULL, lush_assoc_info_NULL, _gpu_root->lmId(), vma, 0, NULL, metric_data);

          cct_dep_graph.addEdge(node, neighbor_node);
          vma_prof_map[vma] = neighbor_node;
          nodes.push(neighbor_node);
        }
      }
    }
  }
}


void CudaAdvisor::pruneCCTGraph(const VMAInstMap &vma_inst_map,
  CCTGraph<CudaParse::InstructionStat *> &inst_dep_graph, VMAProfMap &vma_prof_map,
  int mpi_rank, int thread_id, CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
}


void CudaAdvisor::blameCCTGraph(const VMAInstMap &vma_inst_map,
  CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph,
  int mpi_rank, int thread_id, InstBlames &inst_blames) {
  for (auto iter = cct_dep_graph.nodeBegin(); iter != cct_dep_graph.nodeEnd(); ++iter) {
    auto *node = *iter;
    auto src_vma = node->lmIP();
    auto src_inst = vma_inst_map.at(src_vma);
    auto niter = cct_dep_graph.outgoing_nodes(node);

    std::map<std::string, double> sum;
    if (niter != cct_dep_graph.outgoing_nodes_end()) {
      for (auto *dep_node : niter->second) {
        auto dst_vma = dep_node->lmIP();
        auto *dst_inst = vma_inst_map.at(dst_vma);

        // sum up all neighbor node's instructions
        if (dst_inst->op.find("MEMORY") != std::string::npos) {
          if (dst_inst->op.find("GLOBAL") != std::string::npos) {
            sum[_mem_dep_stall_metric] += demandNodeMetric(mpi_rank, thread_id, dep_node);
          } else if (dst_inst->op.find("LOCAL") != std::string::npos) {
            sum[_mem_dep_stall_metric] += demandNodeMetric(mpi_rank, thread_id, dep_node);
          } else {
            sum[_exec_dep_stall_metric] += demandNodeMetric(mpi_rank, thread_id, dep_node);
          }
        } else {
          sum[_exec_dep_stall_metric] += demandNodeMetric(mpi_rank, thread_id, dep_node);
        }
      }
    }

    // dependent latencies
    if (niter != cct_dep_graph.outgoing_nodes_end()) {
      for (auto *dep_node : niter->second) {
        auto dst_vma = dep_node->lmIP();
        auto *dst_inst = vma_inst_map.at(dst_vma);
        std::string latency_metric;
        if (dst_inst->op.find("MEMORY") != std::string::npos) {
          if (dst_inst->op.find("GLOBAL") != std::string::npos) {
            latency_metric = _mem_dep_stall_metric;
          } else if (dst_inst->op.find("LOCAL") != std::string::npos) {
            latency_metric = _mem_dep_stall_metric;
          } else {
            latency_metric = _exec_dep_stall_metric;
          }
        } else {
          latency_metric = _exec_dep_stall_metric;
        }

        auto latency_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, latency_metric);
        auto latency = node->demandMetric(latency_metric_index);
        if (latency_metric.size() != 0 && latency != 0) {
          double div = sum[latency_metric];
          auto issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric);
          auto neighbor_ratio = dep_node->demandMetric(issue_metric_index) / div;
          // inclusive and exclusive metrics have the same value
          auto in_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + latency_metric, true);
          auto ex_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + latency_metric, false);
          // blame dep_node
          dep_node->demandMetric(in_blame_metric_index) += latency * neighbor_ratio;
          dep_node->demandMetric(ex_blame_metric_index) += latency * neighbor_ratio;
          // one metric id is enough for inst blame analysis
          inst_blames.emplace_back(
            InstructionBlame(src_inst, dst_inst, ex_blame_metric_index, latency * neighbor_ratio));
        }
      }
    }

    // stall latencies
    for (auto &s : _inst_stall_metrics) {
      auto stall_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, s);
      auto stall = node->demandMetric(stall_metric_index);
      if (stall == 0) {
        continue;
      }
      auto in_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + s, true);
      auto ex_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + s, false);
      // inclusive and exclusive metrics have the same value
      // blame itself
      node->demandMetric(in_blame_metric_index) += stall;
      node->demandMetric(ex_blame_metric_index) += stall;
      // one metric id is enough for inst blame analysis
      inst_blames.emplace_back(
        InstructionBlame(src_inst, src_inst, ex_blame_metric_index, stall));
    }
  }

  std::sort(inst_blames.begin(), inst_blames.end());
}


void CudaAdvisor::overlayInstBlames(const std::vector<CudaParse::Function *> &functions,
  const InstBlames &inst_blames, FunctionBlames &function_blames) {
  auto inst_blame_index = 0;
  // Construct function boundaries
  for (auto *function : functions) {
    VMA function_start = std::numeric_limits<VMA>::max();
    VMA function_end = std::numeric_limits<VMA>::min();
    FunctionBlame function_blame(function->id);
    for (auto *block : function->blocks) {
      VMA block_start = block->insts.front()->inst_stat->pc;
      VMA block_end = block->insts.back()->inst_stat->pc; 
      function_start = std::min(block_start, function_start);
      function_end = std::max(block_end, function_end);
      BlockBlame block_blame(block->id, block_start, block_end);
      function_blame.block_blames.push_back(block_blame);
    }
    function_blame.start = function_start;
    function_blame.end = function_end;

    // Accumulate blames
    auto block_blame_index = 0;
    for (; inst_blame_index < inst_blames.size(); ++inst_blame_index) {
      auto &block_blame = function_blame.block_blames[block_blame_index];
      auto &inst_blame = inst_blames[inst_blame_index];

      if (inst_blame.src->pc > function_blame.end) {
        // Go to next function
        break;
      }

      // Find the correponding block
      while (block_blame.end < inst_blame.src->pc) {
        ++block_blame_index;
        block_blame = function_blame.block_blames[block_blame_index];
      }
      if (inst_blame.src->pc >= block_blame.start) {
        block_blame.inst_blames.push_back(inst_blame);
        block_blame.blames[inst_blame.metric_id] += inst_blame.value;
        block_blame.blame += inst_blame.value;
        function_blame.blames[inst_blame.metric_id] += inst_blame.value;
        function_blame.blame += inst_blame.value;
      }
    }

    function_blames.push_back(function_blame);
  }
}


void CudaAdvisor::selectTopBlockBlames(const FunctionBlames &function_blames, BlockBlameQueue &top_block_blames) {
  // TODO(Keren): Clustering similar blocks?
  for (auto &function_blame : function_blames) {
    for (auto &block_blame : function_blame.block_blames) {
      auto &min_block_blame = top_block_blames.top();
      if (min_block_blame.blame < block_blame.blame &&
        top_block_blames.size() > _top_block_blames) {
        top_block_blames.pop();
      }
      top_block_blames.push(block_blame);
    }
  }
}


void CudaAdvisor::rankOptimizers(const VMAStructMap &vma_struct_map,
  BlockBlameQueue &top_block_blames, OptimizerScoreMap &optimizer_scores) {
  while (top_block_blames.empty() == false) {
    auto &block_blame = top_block_blames.top();
    for (auto *optimizer : _intra_warp_optimizers) {
      double score = optimizer->match(vma_struct_map, block_blame);
      optimizer_scores[optimizer] += score;
    }
    for (auto *optimizer : _inter_warp_optimizers) {
      double score = optimizer->match(vma_struct_map, block_blame);
      optimizer_scores[optimizer] += score;
    }
    top_block_blames.pop();
  }
}


void CudaAdvisor::concatAdvise(const OptimizerScoreMap &optimizer_scores) {
  std::map<double, std::vector<CudaOptimizer *> > optimizer_rank;

  for (auto &iter : optimizer_scores) {
    auto *optimizer = iter.first;
    auto score = iter.second;
    optimizer_rank[score].push_back(optimizer);
  }

  size_t rank = 0;
  for (auto &iter : optimizer_rank) {
    for (auto *optimizer : iter.second) {
      ++rank;
      // TODO(Keren): concat advise for the current gpu_root
      _cache = _cache + optimizer->advise();
      if (rank >= _top_optimizers) {
        return;
      }
    }
  }
}


/*
 * Interface methods
 */

void CudaAdvisor::init() {
  if (_inst_stall_metrics.size() != 0) {
    // Init already
    return;
  }

  // Init individual metrics
  _issue_metric = GPU_INST_METRIC_NAME":STL_NONE";
  _inst_metric = GPU_INST_METRIC_NAME;

  _invalid_stall_metric = GPU_INST_METRIC_NAME":STL_INV";
  _tex_stall_metric = GPU_INST_METRIC_NAME":STL_TMEM";
  _ifetch_stall_metric = GPU_INST_METRIC_NAME":STL_IFET";
  _pipe_bsy_stall_metric = GPU_INST_METRIC_NAME":STL_PIPE";
  _mem_thr_stall_metric = GPU_INST_METRIC_NAME":STL_MTHR";
  _nosel_stall_metric = GPU_INST_METRIC_NAME":STL_NSEL";
  _other_stall_metric = GPU_INST_METRIC_NAME":STL_OTHR";
  _sleep_stall_metric = GPU_INST_METRIC_NAME":STL_SLP";
  _cmem_stall_metric = GPU_INST_METRIC_NAME":STL_CMEM";
  
  _inst_stall_metrics.insert(_invalid_stall_metric);
  _inst_stall_metrics.insert(_tex_stall_metric);
  _inst_stall_metrics.insert(_ifetch_stall_metric);
  _inst_stall_metrics.insert(_pipe_bsy_stall_metric);
  _inst_stall_metrics.insert(_mem_thr_stall_metric);
  _inst_stall_metrics.insert(_nosel_stall_metric);
  _inst_stall_metrics.insert(_other_stall_metric);
  _inst_stall_metrics.insert(_sleep_stall_metric);
  _inst_stall_metrics.insert(_cmem_stall_metric);

  _exec_dep_stall_metric = GPU_INST_METRIC_NAME":STL_IDEP";
  _mem_dep_stall_metric = GPU_INST_METRIC_NAME":STL_GMEM";
  _sync_stall_metric = GPU_INST_METRIC_NAME":STL_SYNC";

  _dep_stall_metrics.insert(_exec_dep_stall_metric);
  _dep_stall_metrics.insert(_mem_dep_stall_metric);
  _dep_stall_metrics.insert(_sync_stall_metric);

  for (auto &s : _inst_stall_metrics) {
    _metric_name_prof_map->add("BLAME " + s);
  }

  for (auto &s : _dep_stall_metrics) {
    _metric_name_prof_map->add("BLAME " + s);
  }

  // Init optimizers
  _loop_unroll_optimizer = CudaOptimizerFactory(LOOP_UNROLL);
  _memory_layout_optimizer = CudaOptimizerFactory(MEMORY_LAYOUT);
  _strength_reduction_optimizer = CudaOptimizerFactory(STRENGTH_REDUCTION);

  _intra_warp_optimizers.push_back(_loop_unroll_optimizer);
  _intra_warp_optimizers.push_back(_memory_layout_optimizer);
  _intra_warp_optimizers.push_back(_strength_reduction_optimizer);

  _adjust_registers_optimizer = CudaOptimizerFactory(ADJUST_REGISTERS);
  _adjust_threads_optimizer = CudaOptimizerFactory(ADJUST_THREADS); 

  _inter_warp_optimizers.push_back(_adjust_registers_optimizer);
  _inter_warp_optimizers.push_back(_adjust_threads_optimizer);
}


void CudaAdvisor::config(Prof::CCT::ADynNode *gpu_root) {
  this->_gpu_root = gpu_root;
  // Update kernel characteristics
}


void CudaAdvisor::blame(const std::vector<CudaParse::Function *> &functions, FunctionBlamesMap &function_blames_map) {
  // 1. Map pc to instructions and CCTs
  VMAInstMap vma_inst_map;
  constructVMAInstMap(functions, vma_inst_map);

  VMAProfMap vma_prof_map;
  constructVMAProfMap(vma_prof_map);

  // 2. Init a instruction dependency graph
  // a->b means 'a' depends on 'b'.
  // Latency metrics include issue 'dependency' and latency 'dependency'
  CCTGraph<CudaParse::InstructionStat *> inst_dep_graph;
  initInstDepGraph(functions, vma_inst_map, inst_dep_graph);

  // For each MPI process
  for (auto mpi_rank = 0; mpi_rank < _metric_name_prof_map->num_mpi_ranks();
    ++mpi_rank) {
    // For each CPU thread
    for (auto thread_id = 0; thread_id < _metric_name_prof_map->num_thread_ids(mpi_rank);
      ++thread_id) {
      if (_metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric) == -1) {
        // Skip tracing threads
        continue;
      }

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "[" << mpi_rank << "," << thread_id << "]" << std::endl;
      }

      // 3.0. Init a CCT dependency graph
      CCTGraph<Prof::CCT::ADynNode *> cct_dep_graph;
      for (auto &iter : vma_prof_map) {
        cct_dep_graph.addNode(iter.second);
      }

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "CCT dependency graph before propgation: " << std::endl;
        debugCCTDepGraph(cct_dep_graph);
        std::cout << std::endl;
      }

      // 3.1. Iterative update CCT graph
      propagateCCTGraph(vma_inst_map, inst_dep_graph, vma_prof_map, mpi_rank, thread_id, cct_dep_graph);

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "CCT dependency graph after propgation: " << std::endl;
        debugCCTDepGraph(cct_dep_graph);
        std::cout << std::endl;
      }

      // 3.2. Prune cold paths in CCT graph
      // 1) Latency constraints
      // 2) Opcode constraints
      // 3) Use on the path constraints
      pruneCCTGraph(vma_inst_map, inst_dep_graph, vma_prof_map, mpi_rank, thread_id, cct_dep_graph);

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "CCT dependency graph after pruning: " << std::endl;
        debugCCTDepGraph(cct_dep_graph);
        std::cout << std::endl;
      }

      // 4. Accumulate blames and record significant pairs and paths
      // Apportion based on block latency coverage and def inst issue count
      InstBlames inst_blames;
      blameCCTGraph(vma_inst_map, cct_dep_graph, mpi_rank, thread_id, inst_blames);

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "Debug inst blames: " << std::endl;
        debugInstBlames(inst_blames);
        std::cout << std::endl;
      }

      // 5. Overlay blames
      auto &function_blames = function_blames_map[mpi_rank][thread_id];
      overlayInstBlames(functions, inst_blames, function_blames);
    }
  }
}


void CudaAdvisor::advise(const FunctionBlamesMap &function_blames_map) {
  // 1. Map pc to structs
  VMAStructMap vma_struct_map;
  constructVMAStructMap(vma_struct_map);

  // For each MPI process
  for (auto mpi_rank = 0; mpi_rank < _metric_name_prof_map->num_mpi_ranks();
    ++mpi_rank) {
    // For each CPU thread
    for (auto thread_id = 0; thread_id < _metric_name_prof_map->num_thread_ids(mpi_rank);
      ++thread_id) {
      if (_metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric) == -1) {
        // Skip tracing threads
        continue;
      }

      const FunctionBlames &function_blames = function_blames_map.at(mpi_rank).at(thread_id);

      // 2. Pick top 5 important blocks
      BlockBlameQueue top_block_blames;
      selectTopBlockBlames(function_blames, top_block_blames);

      // 3. Rank optimizers
      OptimizerScoreMap optimizer_scores;
      rankOptimizers(vma_struct_map, top_block_blames, optimizer_scores);

      // 4. Output top 5 advise to _cache
      concatAdvise(optimizer_scores);
    }
  }
}


void CudaAdvisor::save(const std::string &file_name) {
  // clear previous advise
  _cache = "";
}

}  // namespace CallPath

}  // namespace Analysis
