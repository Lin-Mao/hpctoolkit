
#include "IntelGPUInstruction.hpp"
#include <lib/banal/gpu/IntelInstructionAnalyzer.hpp>   // readIntelInstructions


#define DEBUG_CALLPATH_INTELINSTRUCTION 0


//std::vector<Analysis::IntelGPUAdvisor::AdviceTuple>
std::vector<std::string>
overlayIntelGPUInstructionsMain
(
 Prof::CallPath::Profile &prof, const std::vector<std::string> &instruction_files,
 const std::string &gpu_arch
)
{
#if 0
  auto *mgr = prof.metricMgr();
  MetricNameProfMap metric_name_prof_map(mgr);
  metric_name_prof_map.init();

  if (DEBUG_CALLPATH_INTELINSTRUCTION) {
    std::cout << metric_name_prof_map.to_string();
  }

  // Check if prof contains gpu metrics
  // Skip non-gpu prof
  if (metric_name_prof_map.metric_ids(GPU_INST_METRIC_NAME ":STL_NONE").size() == 0) {
    if (DEBUG_CALLPATH_INTELINSTRUCTION) {
      std::cout << "Skip non-gpu prof" << std::endl;
    }
    return std::vector<Analysis::IntelGPUAdvisor::AdviceTuple>();
  }
#endif

  //Analysis::IntelGPUAdvisor gpu_advisor(&prof, &metric_name_prof_map);
  Analysis::IntelGPUAdvisor gpu_advisor(&prof);
  //gpu_advisor.init(gpu_arch);
  // Read instruction files
  for (auto &file : instruction_files) {
    if (DEBUG_CALLPATH_INTELINSTRUCTION) {
      std::cout << std::endl;
      std::cout << "-------------------------------------------------" << std::endl;
      std::cout << "Read instruction file " << file << std::endl;
      std::cout << "-------------------------------------------------" << std::endl;
    }

#if 0
    std::string lm_name = getLMfromInst(file);
    Prof::LoadMap::LMSet_nm::iterator lm_iter;
    if ((lm_iter = prof.loadmap()->lm_find(lm_name)) == prof.loadmap()->lm_end_nm()) {
      if (DEBUG_CALLPATH_INTELINSTRUCTION) {
        std::cout << "Instruction file module " << lm_name << " not found " << std::endl;
      }
      continue;
    }

    Prof::LoadMap::LMId_t lm_id = (*lm_iter)->id();
#endif

    // Step 1: Read metrics
    std::vector<GPUParse::Function *> functions;
    readIntelInstructions(file, functions);

#if 0
    // Step 2: Sort the instructions by PC
    // Assign absolute addresses to instructions
    CudaParse::relocateCudaInstructionStats(functions);

    std::vector<CudaParse::InstructionStat *> inst_stats;
    // Put instructions to a vector
    CudaParse::flatCudaInstructionStats(functions, inst_stats);

    // Step 3: Find new metric names and insert new mappings between from name to prof metric ids
    createMetrics(inst_stats, metric_name_prof_map, *mgr);

    // Step 4: Gather all CCT nodes with lm_id and find GPU roots
    std::vector<VMAStmt> vma_stmts;
    std::set<Prof::CCT::ADynNode *> gpu_kernels;
    std::vector<int> gpu_kernel_index = metric_name_prof_map.metric_ids(GPU_KERNEL_METRIC_NAME);
    auto *prof_root = prof.cct()->root();
    gatherStmts(lm_id, inst_stats.front()->pc, inst_stats.back()->pc, prof_root, vma_stmts,
        gpu_kernel_index, gpu_kernels);

    // Step 5: Lay metrics over prof tree
    associateInstStmts(vma_stmts, inst_stats, metric_name_prof_map);

    gpu_advisor.configInst(lm_name, functions);

    // Step 6: Make advise
    // Find each GPU calling context, make recommendation for each calling context
    for (auto *gpu_kernel : gpu_kernels) {
      auto *gpu_root = dynamic_cast<Prof::CCT::ADynNode *>(gpu_kernel->parent());

      // Pass current gpu root
      gpu_advisor.configGPURoot(gpu_root, gpu_kernel);

      // <mpi_rank, <thread_id, <blames>>>
      CCTBlames cct_blames;

      // Blame latencies
      gpu_advisor.blame(cct_blames);

      // Make advise for the calling context and cache result
      gpu_advisor.advise(cct_blames);
    }
#endif

    if (DEBUG_CALLPATH_INTELINSTRUCTION) {
      std::cout << "Finish reading instruction file " << file << std::endl;
    }
  }

  return gpu_advisor.get_advice();
}
