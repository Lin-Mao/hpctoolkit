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
#include <iomanip>
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

#define DEBUG_CALLPATH_CUDAADVISOR 0
#define DEBUG_CALLPATH_CUDAADVISOR_DETAILS 0

// Define an offset in cubin
// It varies for different cubins.
// It is hard to figure out a way to find the offset automatically,
// because we could have device functions before global functions in cubins
#define DEBUG_OFFSET(x) (x - 0)

namespace Analysis {

namespace CallPath {

/*
 * Debug methods
 */

void CudaAdvisor::debugCCTDepGraph(int mpi_rank, int thread_id, CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  std::cout << "Nodes (" << cct_dep_graph.size() << ")" << std::endl;
  std::cout << "Edges:" << std::endl;
  for (auto it = cct_dep_graph.edgeBegin(); it != cct_dep_graph.edgeEnd(); ++it) {
    auto *from_node = it->from;
    auto *to_node = it->to;

    auto from_vma = from_node->lmIP();
    auto to_vma = to_node->lmIP();

    std::cout << std::hex << "0x" << DEBUG_OFFSET(from_vma) << " -> 0x" <<
      DEBUG_OFFSET(to_vma) << std::dec << std::endl;
  }

  std::cout << std::endl << "Outstanding Latencies:" << std::endl;

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

  std::cout << "Exec_deps" << std::endl;
  for (auto &iter : exec_dep_vmas) {
    if (iter.first == 0) {
      continue;
    }
    std::cout << iter.first << ": ";
    for (auto vma : iter.second) {
      std::cout << std::hex << "0x" << DEBUG_OFFSET(vma) << std::dec << ", ";
    }
    std::cout << std::endl;
  }

  std::cout << "Mem_deps" << std::endl;
  for (auto &iter : mem_dep_vmas) {
    if (iter.first == 0) {
      continue;
    }
    std::cout << iter.first << ": ";
    for (auto vma : iter.second) {
      std::cout << std::hex << "0x" << DEBUG_OFFSET(vma) << std::dec << ", ";
    }
    std::cout << std::endl;
  }
}


void CudaAdvisor::debugCCTDepGraphSinglePath(CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  double total_path = 0.0;
  double single_path = 0.0;

  for (auto it = cct_dep_graph.nodeBegin(); it != cct_dep_graph.nodeEnd(); ++it) {
    auto *node = *it;

    auto out = cct_dep_graph.outgoing_nodes_size(node);
    if (out == 1) {
      ++single_path;
    }
    total_path += out;
  }

  if (total_path == 0) {
    std::cout << "No path" << std::endl;
  } else {
    std::cout << std::setprecision(5) << single_path / total_path << std::endl;
  }
}


void CudaAdvisor::debugCCTDepPaths(CCTEdgePathMap &cct_edge_path_map) {
  for (auto &from_iter : cct_edge_path_map) {
    auto from_vma = from_iter.first;
    for (auto &to_iter : from_iter.second) {
      if (to_iter.second.size() == 0) {
        // Skip zero path entries
        continue;
      }
      auto to_vma = to_iter.first;
      std::cout << std::hex << "0x" << DEBUG_OFFSET(from_vma) << " -> 0x" <<
        DEBUG_OFFSET(to_vma) << std::dec << ":" << std::endl;
      for (auto &path : to_iter.second) {
        std::cout << "[";
        for (auto *b : path) {
          auto front_vma = b->insts.front()->inst_stat->pc;
          std::cout << std::hex << "0x" << DEBUG_OFFSET(front_vma) << ",";
        }
        std::cout << "]" << std::endl;
      }
    }
  }
}


void CudaAdvisor::debugInstDepGraph() {
  std::cout << "Nodes (" << _inst_dep_graph.size() << ")" << std::endl;
  std::cout << "Edges:" << std::endl;
  for (auto it = _inst_dep_graph.edgeBegin(); it != _inst_dep_graph.edgeEnd(); ++it) {
    auto *from_node = it->from;
    auto *to_node = it->to;

    auto from_vma = from_node->pc;
    auto to_vma = to_node->pc;

    std::cout << std::hex << "0x" << DEBUG_OFFSET(from_vma) << " -> 0x" <<
      DEBUG_OFFSET(to_vma) << std::dec << std::endl;
  }
}


void CudaAdvisor::debugInstBlames(InstBlames &inst_blames) {
  for (auto &inst_blame : inst_blames) {
    std::cout << std::hex << inst_blame.dst->pc << " ->";
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

int CudaAdvisor::demandNodeMetric(int mpi_rank, int thread_id, Prof::CCT::ADynNode *node) {
  auto in_issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric, true);
  auto ex_issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric, false);
  auto in_inst_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric, true);
  auto ex_inst_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric, false);

  // If instruction issue is not sampled, assign it one issue
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


void CudaAdvisor::initCCTDepGraph(int mpi_rank, int thread_id, CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  // Init nodes
  for (auto &iter : _vma_prop_map) {
    auto *prof_node = iter.second.prof_node;
    if (prof_node != NULL) {
      cct_dep_graph.addNode(prof_node);
    }
  }

  std::vector<std::pair<Prof::CCT::ADynNode *, Prof::CCT::ADynNode *> > edge_vec;
  // Insert all possible edges
  // If a node has samples, we find all possible causes.
  // Even for instructions that are not sampled, we assign them one issued sample, but no latency sample.
  // Since no latency sample is associated with these nodes, we do not need to propogate in the grpah.
  for (auto iter = cct_dep_graph.nodeBegin(); iter != cct_dep_graph.nodeEnd(); ++iter) {
    auto *node = *iter;
    auto node_vma = node->lmIP();

    // Find its dependent instructions
    auto *node_inst = _vma_prop_map[node_vma].inst;
    auto inst_iter = _inst_dep_graph.incoming_nodes(node_inst);

    if (inst_iter != _inst_dep_graph.incoming_nodes_end()) {
      for (auto *inst : inst_iter->second) {
        auto vma = inst->pc;
        auto vma_prop_iter = _vma_prop_map.find(vma);

        Prof::CCT::ADynNode *prof_node = NULL;
        if (vma_prop_iter->second.prof_node == NULL) {
          // Create a new prof node
          Prof::Metric::IData metric_data(_prof->metricMgr()->size());
          metric_data.clearMetrics();

          prof_node = new Prof::CCT::Stmt(node->parent(), HPCRUN_FMT_CCTNodeId_NULL,
            lush_assoc_info_NULL, _gpu_root->lmId(), vma, 0, NULL, metric_data);
          vma_prop_iter->second.prof_node = prof_node;
        } else {
          prof_node = vma_prop_iter->second.prof_node;
        }

        edge_vec.push_back(std::pair<Prof::CCT::ADynNode *, Prof::CCT::ADynNode *>(prof_node, node));
      }
    }
  }

  for (auto &edge : edge_vec) {
    auto *from_node = edge.first;
    auto *to_node = edge.second;
    
    cct_dep_graph.addEdge(from_node, to_node);
  }
}


void CudaAdvisor::pruneCCTDepGraphOpcode(int mpi_rank, int thread_id,
  CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph) {
  // Profile single path coverage
  if (DEBUG_CALLPATH_CUDAADVISOR) {
    std::cout << "Single path coverage before opcode constraints:" << std::endl;
    debugCCTDepGraphSinglePath(cct_dep_graph);
    std::cout << std::endl;
  }

  auto mem_dep_stall_id = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _mem_dep_stall_metric);
  auto exec_dep_stall_id = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _exec_dep_stall_metric);

  // Opcode constraints
  std::vector<std::set<CCTEdge<Prof::CCT::ADynNode *> >::iterator> remove_edges;
  for (auto iter = cct_dep_graph.edgeBegin(); iter != cct_dep_graph.edgeEnd(); ++iter) {
    auto edge = *iter;
    auto *from = edge.from;
    auto from_vma = from->lmIP();
    auto *to = edge.to;
    auto *from_inst = _vma_prop_map[from_vma].inst;

    if (from_inst->op.find("MEMORY") != std::string::npos) {
      if (from_inst->op.find(".SHARED") != std::string::npos) {
        // Shared memory instructions only cause short_scoreboard wait
        if (to->demandMetric(exec_dep_stall_id) == 0) {
          remove_edges.push_back(iter);
        }
      } else {
        // L1TEX Memory instructions only cause memory dep
        if (to->demandMetric(mem_dep_stall_id) == 0) {
          remove_edges.push_back(iter);
        }
      }
    } else {
      // XXX(Keren): Other type instructions cause either short_scoreboard or
      // fixed dependency 
      if (to->demandMetric(exec_dep_stall_id) == 0) {
        remove_edges.push_back(iter);
      }
    }
  }

  for (auto &iter : remove_edges) {
    cct_dep_graph.removeEdge(iter);
  }

  // Profile single path coverage
  if (DEBUG_CALLPATH_CUDAADVISOR) {
    std::cout << "Single path coverage after opcode constraints:" << std::endl;
    debugCCTDepGraphSinglePath(cct_dep_graph);
    std::cout << std::endl;
  }
}


// DFS procedure
void CudaAdvisor::trackReg(int from_vma, int to_vma, int reg,
  CudaParse::Block *from_block, CudaParse::Block *to_block,
  int latency_throughput, int latency, std::set<CudaParse::Block *> &visited_blocks,
  std::vector<CudaParse::Block *> &path, std::vector<std::vector<CudaParse::Block *>> &paths) {
  // The current block has been visited
  if (visited_blocks.find(from_block) != visited_blocks.end()) {
    return;
  }
  visited_blocks.insert(from_block);
  path.push_back(from_block);

  auto front_vma = from_block->insts.front()->inst_stat->pc;
  auto back_vma = from_block->insts.back()->inst_stat->pc;

  // Accumulate throughput
  bool find_def = false;
  bool hidden = false;
  auto start_vma = front_vma;
  auto end_vma = back_vma;

  // [front_vma, from_vma, back_vma]
  if (from_vma <= back_vma && from_vma >= front_vma) {
    start_vma = from_vma;
  }

  // [front_vma, to_vma, back_vma]
  if (to_vma <= back_vma && to_vma >= front_vma) {
    end_vma = to_vma - _arch->inst_size();
  }

  // If from_vma and to_vma are in the same block but from_vma >= to_vma
  // It indicates we have a loop
  if (from_vma <= back_vma && from_vma >= front_vma &&
    to_vma <= back_vma && to_vma >= front_vma &&
    from_vma >= to_vma) {
    end_vma = back_vma;
  }

  // Iterate inst until reaching the use inst
  while (start_vma <= end_vma) {
    auto *inst = _vma_prop_map[start_vma].inst;
    latency_throughput += _vma_prop_map[start_vma].latency_throughput;

    // 1. Reg on path constraint
    if (std::find(inst->srcs.begin(), inst->srcs.end(), reg) != inst->srcs.end()) {
      find_def = true;
      break;
    }

    // 2. Latency constraint
    if (latency_throughput >= latency) {
      hidden = true;
      break;
    }

    start_vma += _arch->inst_size();
  }

  // If we find a valid path
  if (find_def == false && hidden == false) {
    // Reach the final block
    // [front_vma, to_vma, back_vma]
    if (to_vma <= back_vma && to_vma >= front_vma) {
      paths.push_back(path);
    } else {
      for (auto *target : to_block->targets) {
        if (target->type != CudaParse::TargetType::CALL && target->type != CudaParse::TargetType::CALL_FT) {
          // We do not need from_vma anymore
          trackReg(0, to_vma, reg, target->block, to_block, latency_throughput, latency,
            visited_blocks, path, paths);
        }
      }
    }
  }

  visited_blocks.erase(from_block);
  path.pop_back();
}


void CudaAdvisor::pruneCCTDepGraphLatency(CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph,
  CCTEdgePathMap &cct_edge_path_map) {
  // Profile single path coverage
  if (DEBUG_CALLPATH_CUDAADVISOR) {
    std::cout << "Single path coverage before path constraints:" << std::endl;
    debugCCTDepGraphSinglePath(cct_dep_graph);
    std::cout << std::endl;
  }

  std::vector<std::set<CCTEdge<Prof::CCT::ADynNode *> >::iterator> remove_edges;
  for (auto iter = cct_dep_graph.edgeBegin(); iter != cct_dep_graph.edgeEnd(); ++iter) {
    auto edge = *iter;
    auto *from = edge.from;
    auto from_vma = from->lmIP();
    auto *to = edge.to;
    auto to_vma = to->lmIP();
    auto *to_inst = _vma_prop_map.at(to_vma).inst;
    auto *from_inst = _vma_prop_map.at(from_vma).inst;

    for (auto dst : from_inst->dsts) {
      // Find the dst reg that causes dependency
      auto assign_iter = to_inst->assign_pcs.find(dst);
      if (assign_iter != to_inst->assign_pcs.end()) {
        // Search for all possible paths from dst to src.
        // Since the single path rate here is expected to be high,
        // The following code section only executes few times.
        // Two constraints:
        // 1. A path is eliminated if a instruction on the way uses dst
        // 2. A path is eliminated if the throughput cycles is greater than latency
        auto *from_block = _vma_prop_map.at(from_vma).block;
        auto *to_block = _vma_prop_map.at(to_vma).block;
        std::set<CudaParse::Block *> visited_blocks;
        std::vector<CudaParse::Block *> path;
        std::vector<std::vector<CudaParse::Block *>> paths;
        auto latency = _vma_prop_map.at(from_vma).latency_upper;
        trackReg(from_vma, to_vma, dst, from_block, to_block,
          0, latency, visited_blocks, path, paths);

        // Merge paths
        for (auto &path : paths) {
          bool new_path = true;
          // Existing paths
          for (auto &p : cct_edge_path_map[from_vma][to_vma]) {
            if (path.size() != p.size()) {
              continue;
            }
            // Same size paths, comparing every element
            bool same_path = true;
            for (size_t i = 0; i < p.size(); ++i) {
              if (p[i] != path[i]) {
                same_path = false;
                break;
              }
            }
            if (same_path == true) {
              new_path = false;
              break;
            }
          }
          if (new_path == true) {
            cct_edge_path_map[from_vma][to_vma].push_back(path);
          }
        }
      }
    }

    // If there's no satisfied path
    if (cct_edge_path_map[from_vma][to_vma].size() == 0) {
      // This edge can be removed
      remove_edges.push_back(iter);
    }
  }

  for (auto &iter : remove_edges) {
    cct_dep_graph.removeEdge(iter);
  }

  if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
    std::cout << "CCT dependency paths: " << std::endl;
    debugCCTDepPaths(cct_edge_path_map);
    std::cout << std::endl;
  }

  // Profile single path coverage
  if (DEBUG_CALLPATH_CUDAADVISOR) {
    std::cout << "Single path coverage after path constraints:" << std::endl;
    debugCCTDepGraphSinglePath(cct_dep_graph);
    std::cout << std::endl;
  }
}


double CudaAdvisor::computePathStallRatio(
  int mpi_rank, int thread_id, int to_vma, int from_vma,
  std::vector<CudaParse::Block *> &path) {
  double inst = 0.0;
  double stall = 0.0;

  auto inst_metric_index = _metric_name_prof_map->metric_id(
    mpi_rank, thread_id, _inst_metric, true);
  auto stall_metric_index = _metric_name_prof_map->metric_id(
    mpi_rank, thread_id, _stall_metric, false);

  for (size_t i = 0; i < path.size(); ++i) {
    auto *block = path[i];
    auto front_vma = block->insts.front()->inst_stat->pc;
    auto back_vma = block->insts.back()->inst_stat->pc;
    auto start_vma = front_vma; 
    auto end_vma = back_vma;
    if (i == 0) {
      start_vma = to_vma;
    }
    if (i == path.size() - 1) {
      end_vma = from_vma;
    }
    for (size_t j = start_vma; j < end_vma; j += _arch->inst_size()) {
      auto *prof_node = _vma_prop_map[j].prof_node;
      if (prof_node != NULL) {
        inst += prof_node->demandMetric(inst_metric_index);
        stall += prof_node->demandMetric(stall_metric_index);
      }
    }
  }

  if (inst == 0.0) {
    return 0.0;
  } else {
    return stall / inst;
  }
}


void CudaAdvisor::blameCCTDepGraph(int mpi_rank, int thread_id,
  CCTGraph<Prof::CCT::ADynNode *> &cct_dep_graph, CCTEdgePathMap &cct_edge_path_map,
  InstBlames &inst_blames) {
  for (auto iter = cct_dep_graph.nodeBegin(); iter != cct_dep_graph.nodeEnd(); ++iter) {
    auto *to_node = *iter;
    auto to_vma = to_node->lmIP();
    auto to_inst = _vma_prop_map[to_vma].inst;
    auto innode_iter = cct_dep_graph.incoming_nodes(to_node);

    std::map<std::string, std::vector<Prof::CCT::ADynNode *> > latency_prof_nodes;
    if (innode_iter != cct_dep_graph.incoming_nodes_end()) {
      for (auto *from_node : innode_iter->second) {
        // Ensure from node is issued at least once
        demandNodeMetric(mpi_rank, thread_id, from_node);

        auto from_vma = from_node->lmIP();
        auto *from_inst = _vma_prop_map[from_vma].inst;

        // Sum up all neighbors' issue count
        if (from_inst->op.find("MEMORY") != std::string::npos) {
          if (from_inst->op.find(".GLOBAL") != std::string::npos ||
              from_inst->op.find(".LOCAL") != std::string::npos) {
            // Global and local memory result in memory dependencies
            latency_prof_nodes[_mem_dep_stall_metric].push_back(from_node);
          } else if (from_inst->op.find(".SHARED") != std::string::npos) {
            // Shared memory
            latency_prof_nodes[_exec_dep_stall_metric].push_back(from_node);
          } 
          // Constant memory latency is attributed to inst itself
        } else {
          latency_prof_nodes[_exec_dep_stall_metric].push_back(from_node);
        }
      }
    }

    // Dependent latencies
    for (auto &latency_iter : latency_prof_nodes) {
      auto &latency_metric = latency_iter.first;
      auto &from_nodes = latency_iter.second;

      auto issue_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _issue_metric);
      auto latency_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, latency_metric);
      auto latency = to_node->demandMetric(latency_metric_index);

      // We must have found a correponding latency, otherwise the edge is remove in pruning
      assert(latency != 0);

      // inclusive and exclusive metrics have the same value
      auto in_blame_metric_index = _metric_name_prof_map->metric_id(
        mpi_rank, thread_id, "BLAME " + latency_metric, true);
      auto ex_blame_metric_index = _metric_name_prof_map->metric_id(
        mpi_rank, thread_id, "BLAME " + latency_metric, false);

      if (from_nodes.size() == 1) {
        // Only have one path
        auto *from_node = from_nodes[0];
        auto from_vma = from_node->lmIP();
        auto *from_inst = _vma_prop_map[from_vma].inst;
        // blame from_node
        from_node->demandMetric(in_blame_metric_index) += latency;
        from_node->demandMetric(ex_blame_metric_index) += latency;
        // One metric id is enough for inst blame analysis
        inst_blames.emplace_back(
          InstructionBlame(to_inst, from_inst, ex_blame_metric_index, latency));
      } else {
        std::map<Prof::CCT::ADynNode *, double> stall_ratios;
        std::map<Prof::CCT::ADynNode *, double> issues;
        double stall_ratio_sum = 0.0;
        double issue_sum = 0.0;

        for (auto *from_node : from_nodes) {
          auto from_vma = from_node->lmIP();
          auto ps = cct_edge_path_map[from_vma][to_vma];

          for (auto &path : ps) {
            auto stall_ratio = computePathStallRatio(mpi_rank, thread_id, to_vma, from_vma, path);
            stall_ratios[from_node] += stall_ratio;
            stall_ratio_sum += stall_ratio;
          }
          auto issue = from_node->demandMetric(issue_metric_index);
          issues[from_node] = issue;
          issue_sum += issue;
        }

        // Apportion based on issue and stall ratios
        for (auto *from_node : from_nodes) {
          auto from_vma = from_node->lmIP();
          auto *from_inst = _vma_prop_map[from_vma].inst;

          auto blame_latency = latency * (stall_ratios[from_node] / stall_ratio_sum) *
            (issues[from_node] / issue_sum);
          from_node->demandMetric(in_blame_metric_index) += blame_latency;
          from_node->demandMetric(ex_blame_metric_index) += blame_latency;

          // One metric id is enough for inst blame analysis
          inst_blames.emplace_back(
            InstructionBlame(to_inst, from_inst, ex_blame_metric_index, blame_latency));
        }
      }
    }

    // Stall latencies
    for (auto &s : _inst_stall_metrics) {
      auto stall_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, s);
      auto stall = to_node->demandMetric(stall_metric_index);
      if (stall == 0) {
        continue;
      }
      auto in_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + s, true);
      auto ex_blame_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, "BLAME " + s, false);
      // inclusive and exclusive metrics have the same value
      // blame itself
      to_node->demandMetric(in_blame_metric_index) += stall;
      to_node->demandMetric(ex_blame_metric_index) += stall;
      // one metric id is enough for inst blame analysis
      inst_blames.emplace_back(
        InstructionBlame(to_inst, to_inst, ex_blame_metric_index, stall));
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


void CudaAdvisor::rankOptimizers(BlockBlameQueue &top_block_blames, OptimizerScoreMap &optimizer_scores) {
  while (top_block_blames.empty() == false) {
    auto &block_blame = top_block_blames.top();
    for (auto *optimizer : _intra_warp_optimizers) {
      double score = optimizer->match(block_blame);
      optimizer_scores[optimizer] += score;
    }
    for (auto *optimizer : _inter_warp_optimizers) {
      double score = optimizer->match(block_blame);
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
  _stall_metric = GPU_INST_METRIC_NAME":STL_ANY";
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

// 1. Init static instruction information in vma_prop_map
// 2. Init an instruction dependency graph
void CudaAdvisor::configInst(const std::vector<CudaParse::Function *> &functions) {
  _vma_prop_map.clear();
  _inst_dep_graph.clear();

  // Property map
  for (auto *function : functions) {
    for (auto *block : function->blocks) {
      for (auto *_inst : block->insts) {
        auto *inst = _inst->inst_stat;

        VMAProperty property;

        property.inst = inst;
        property.vma = inst->pc;
        property.function = function;
        property.block = block;

        _vma_prop_map[property.vma] = property;
      }
    }
  }

  // Instruction Graph
  for (auto &iter : _vma_prop_map) {
    auto *inst = iter.second.inst;
    _inst_dep_graph.addNode(inst);

    // Add latency dependencies
    for (auto &inst_iter : inst->assign_pcs) {
      for (auto pc : inst_iter.second) {
        auto *dep_inst = _vma_prop_map[pc].inst;
        _inst_dep_graph.addEdge(dep_inst, inst);
      }
    }
  }
  
  // Static struct
  auto *struct_root = _prof->structure()->root();
  Prof::Struct::ANodeIterator struct_iter(struct_root, NULL/*filter*/, true/*leavesOnly*/,
    IteratorStack::PreOrder);
  for (Prof::Struct::ANode *n = NULL; (n = struct_iter.current()); ++struct_iter) {
    if (n->type() == Prof::Struct::ANode::TyStmt) {
      auto *stmt = dynamic_cast<Prof::Struct::Stmt *>(n);
      for (auto &vma_interval : stmt->vmaSet()) {
        auto vma = vma_interval.beg();
        if (_vma_prop_map.find(vma) != _vma_prop_map.end()) {
          _vma_prop_map[vma].struct_node = stmt;
        }
      }
    }
  }

  if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
    std::cout << "Instruction dependency graph: " << std::endl;
    debugInstDepGraph();
    std::cout << std::endl;
  }
}


void CudaAdvisor::configGPURoot(Prof::CCT::ADynNode *gpu_root) {
  // Update current root
  this->_gpu_root = gpu_root;

  // TODO(Keren): Update kernel characteristics

  // TODO(Keren): Find device tag under the root and use the corresponding archtecture
  // Problem: currently we only have device tags for call instructions
  this->_arch = new SM70(); 

  // Update vma->latency mapping
  for (auto &iter : _vma_prop_map) {
    auto &prop = iter.second;
    auto *inst = prop.inst;

    auto latency = _arch->latency(inst->op);
    prop.latency_lower = latency.first;
    prop.latency_upper = latency.second;
    prop.latency_throughput = _arch->throughput(inst->op);
    // Clear previous vma->prof mapping
    prop.prof_node = NULL;
  }

  // Update vma->prof mapping
  Prof::CCT::ANodeIterator prof_it(_gpu_root, NULL/*filter*/, true/*leavesOnly*/,
    IteratorStack::PreOrder);
  for (Prof::CCT::ANode *n = NULL; (n = prof_it.current()); ++prof_it) {
    Prof::CCT::ADynNode* n_dyn = dynamic_cast<Prof::CCT::ADynNode*>(n);
    if (n_dyn) {
      auto vma = n_dyn->lmIP();
      _vma_prop_map[vma].prof_node = n_dyn;
    }
  }
}


void CudaAdvisor::blame(FunctionBlamesMap &function_blames_map) {
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

      bool gpu_metric = false;
      auto inst_metric_index = _metric_name_prof_map->metric_id(mpi_rank, thread_id, _inst_metric);
      for (auto &iter : _vma_prop_map) {
        auto *prof_node = iter.second.prof_node;
        if (prof_node != NULL && prof_node->demandMetric(inst_metric_index) != 0.0) {
          gpu_metric = true;
          break;
        }
      }
      if (gpu_metric == false) {
        // Skip CPU threads without GPU kernels
        continue;
      }

      if (DEBUG_CALLPATH_CUDAADVISOR) {
        std::cout << "[" << mpi_rank << "," << thread_id << "]" << std::endl << std::endl;
      }

      // 1. Init a CCT dependency graph
      CCTGraph<Prof::CCT::ADynNode *> cct_dep_graph;
      initCCTDepGraph(mpi_rank, thread_id, cct_dep_graph);

      if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
        std::cout << "CCT dependency graph after propgation: " << std::endl;
        debugCCTDepGraph(mpi_rank, thread_id, cct_dep_graph);
        std::cout << std::endl;
      }

      // 2. Prune cold paths in CCT graph
      // Opcode constraints
      pruneCCTDepGraphOpcode(mpi_rank, thread_id, cct_dep_graph);

      if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
        std::cout << "CCT dependency graph after opcode pruning: " << std::endl;
        debugCCTDepGraph(mpi_rank, thread_id, cct_dep_graph);
        std::cout << std::endl;
      }

      // Path latency constraints
      CCTEdgePathMap cct_edge_path_map;
      pruneCCTDepGraphLatency(cct_dep_graph, cct_edge_path_map);

      if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
        std::cout << "CCT dependency graph after latency pruning: " << std::endl;
        debugCCTDepGraph(mpi_rank, thread_id, cct_dep_graph);
        std::cout << std::endl;
      }

      // 3. Accumulate blames and record significant pairs and paths
      // Apportion based on block latency coverage and def inst issue count
      InstBlames inst_blames;
      //blameCCTDepGraph(mpi_rank, thread_id, cct_dep_graph, cct_edge_path_map, inst_blames);

      if (DEBUG_CALLPATH_CUDAADVISOR_DETAILS) {
        std::cout << "Inst blames: " << std::endl;
        debugInstBlames(inst_blames);
        std::cout << std::endl;
      }

      //// 5. Overlay blames
      //auto &function_blames = function_blames_map[mpi_rank][thread_id];
      //overlayInstBlames(functions, inst_blames, function_blames);
    }
  }
}


void CudaAdvisor::advise(const FunctionBlamesMap &function_blames_map) {
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
      rankOptimizers(top_block_blames, optimizer_scores);

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