/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2021 Nikolai Maas <nikolai.maas@student.kit.edu>
 * Copyright (C) 2019 Tobias Heuer <tobias.heuer@kit.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

#include "mt-kahypar/partition/recursive_bipartitioning.h"

#include "tbb/task_group.h"

#include <algorithm>
#include <vector>

#include "mt-kahypar/definitions.h"
#include "mt-kahypar/macros.h"
#include "mt-kahypar/partition/multilevel.h"

#include "mt-kahypar/parallel/memory_pool.h"
#include "mt-kahypar/utils/randomize.h"
#include "mt-kahypar/utils/utilities.h"
#include "mt-kahypar/utils/timer.h"

#include "mt-kahypar/partition/metrics.h"

/** RecursiveBipartitioning Implementation Details
  *
  * Note, the recursive bipartitioning algorithm is written in TBBInitializer continuation style. The TBBInitializer
  * continuation style is especially useful for recursive patterns. Each task defines its continuation
  * task. A continuation task defines how computation should continue, if all its child tasks are completed.
  * As a consequence, tasks can be spawned without waiting for their completion, because the continuation
  * task is automatically invoked if all child tasks are terminated. Therefore, no thread will waste CPU
  * time while waiting for their recursive tasks to complete.
  *
  * ----------------------
  * The recursive bipartitioning algorithm starts by spawning the root RecursiveMultilevelBipartitioningTask. The RecursiveMultilevelBipartitioningTask
  * spawns a MultilevelBipartitioningTask that bisects the hypergraph (multilevel-fashion). Afterwards, the MultilevelBipartitioningContinuationTask continues
  * and applies the bisection to the hypergraph and spawns two RecursiveBipartitioningChildTasks. Both are responsible for exactly one block of
  * the partition. The RecursiveBipartitioningChildTask extracts its corresponding block as unpartitioned hypergraph and spawns
  * recursively a RecursiveMultilevelBipartitioningTask for that hypergraph. Once that RecursiveMultilevelBipartitioningTask is completed, a
  * RecursiveBipartitioningChildContinuationTask is started and the partition of the recursion is applied to the original hypergraph.
*/

namespace mt_kahypar {


struct OriginalHypergraphInfo {

  double computeAdaptiveEpsilon(const HypernodeWeight current_hypergraph_weight,
                                const PartitionID current_k) const {
    if ( current_hypergraph_weight == 0 ) {
      return 0.0;
    } else {
      double base = ceil(static_cast<double>(original_hypergraph_weight) / original_k)
                    / ceil(static_cast<double>(current_hypergraph_weight) / current_k)
                    * (1.0 + original_epsilon);
      double adaptive_epsilon = std::min(0.99, std::max(std::pow(base, 1.0 /
                                                                        ceil(log2(static_cast<double>(current_k)))) - 1.0,0.0));
      return adaptive_epsilon;
    }
  }

  const HypernodeWeight original_hypergraph_weight;
  const PartitionID original_k;
  const double original_epsilon;
};

namespace tmp {

  static constexpr bool debug = false;

  Context setupBipartitioningContext(const Hypergraph& hypergraph,
                                     const Context& context,
                                     const OriginalHypergraphInfo& info) {
    Context b_context(context);

    b_context.partition.k = 2;
    b_context.partition.verbose_output = false;
    b_context.initial_partitioning.mode = Mode::direct;
    // TODO(maas): other type for context?
    if (context.partition.mode == Mode::direct) {
      b_context.type = ContextType::initial_partitioning;
    }

    // Setup Part Weights
    const HypernodeWeight total_weight = hypergraph.totalWeight();
    const PartitionID k = context.partition.k;
    const PartitionID k0 = k / 2 + (k % 2 != 0 ? 1 : 0);
    const PartitionID k1 = k / 2;
    ASSERT(k0 + k1 == context.partition.k);
    if ( context.partition.use_individual_part_weights ) {
      const HypernodeWeight max_part_weights_sum = std::accumulate(context.partition.max_part_weights.cbegin(),
                                                                  context.partition.max_part_weights.cend(), 0);
      const double weight_fraction = total_weight / static_cast<double>(max_part_weights_sum);
      ASSERT(weight_fraction <= 1.0);
      b_context.partition.perfect_balance_part_weights.clear();
      b_context.partition.max_part_weights.clear();
      HypernodeWeight perfect_weight_p0 = 0;
      for ( PartitionID i = 0; i < k0; ++i ) {
        perfect_weight_p0 += ceil(weight_fraction * context.partition.max_part_weights[i]);
      }
      HypernodeWeight perfect_weight_p1 = 0;
      for ( PartitionID i = k0; i < k; ++i ) {
        perfect_weight_p1 += ceil(weight_fraction * context.partition.max_part_weights[i]);
      }
      // In the case of individual part weights, the usual adaptive epsilon formula is not applicable because it
      // assumes equal part weights. However, by observing that ceil(current_weight / current_k) is the current
      // perfect part weight and (1 + epsilon)ceil(original_weight / original_k) is the maximum part weight,
      // we can derive an equivalent formula using the sum of the perfect part weights and the sum of the
      // maximum part weights.
      // Note that the sum of the perfect part weights might be unequal to the hypergraph weight due to rounding.
      // Thus, we need to use the former instead of using the hypergraph weight directly, as otherwise it could
      // happen that (1 + epsilon)perfect_part_weight > max_part_weight because of rounding issues.
      const double base = max_part_weights_sum / static_cast<double>(perfect_weight_p0 + perfect_weight_p1);
      b_context.partition.epsilon = total_weight == 0 ? 0 : std::min(0.99, std::max(std::pow(base, 1.0 /
                                                                    ceil(log2(static_cast<double>(k)))) - 1.0,0.0));
      b_context.partition.perfect_balance_part_weights.push_back(perfect_weight_p0);
      b_context.partition.perfect_balance_part_weights.push_back(perfect_weight_p1);
      b_context.partition.max_part_weights.push_back(
              round((1 + b_context.partition.epsilon) * perfect_weight_p0));
      b_context.partition.max_part_weights.push_back(
              round((1 + b_context.partition.epsilon) * perfect_weight_p1));
    } else {
      b_context.partition.epsilon = info.computeAdaptiveEpsilon(total_weight, k);

      b_context.partition.perfect_balance_part_weights.clear();
      b_context.partition.max_part_weights.clear();
      b_context.partition.perfect_balance_part_weights.push_back(
              std::ceil(k0 / static_cast<double>(k) * static_cast<double>(total_weight)));
      b_context.partition.perfect_balance_part_weights.push_back(
              std::ceil(k1 / static_cast<double>(k) * static_cast<double>(total_weight)));
      b_context.partition.max_part_weights.push_back(
              (1 + b_context.partition.epsilon) * b_context.partition.perfect_balance_part_weights[0]);
      b_context.partition.max_part_weights.push_back(
              (1 + b_context.partition.epsilon) * b_context.partition.perfect_balance_part_weights[1]);
    }
    b_context.setupContractionLimit(total_weight);
    b_context.setupSparsificationParameters();
    b_context.setupThreadsPerFlowSearch();

    return b_context;
  }

  Context setupRecursiveBipartitioningContext(const Context& context,
                                              const PartitionID k0, const PartitionID k1,
                                              const double degree_of_parallelism) {
    ASSERT((k1 - k0) >= 2);
    Context rb_context(context);
    rb_context.partition.k = k1 - k0;
    if (context.partition.mode == Mode::direct) {
      rb_context.type = ContextType::initial_partitioning;
    }

    rb_context.partition.perfect_balance_part_weights.assign(rb_context.partition.k, 0);
    rb_context.partition.max_part_weights.assign(rb_context.partition.k, 0);
    for ( PartitionID part_id = k0; part_id < k1; ++part_id ) {
      rb_context.partition.perfect_balance_part_weights[part_id - k0] =
              context.partition.perfect_balance_part_weights[part_id];
      rb_context.partition.max_part_weights[part_id - k0] =
              context.partition.max_part_weights[part_id];
    }

    rb_context.shared_memory.degree_of_parallelism *= degree_of_parallelism;

    return rb_context;
  }

  void recursively_bipartition_block(PartitionedHypergraph& phg,
                                     const Context& context,
                                     const PartitionID block, const PartitionID k0, const PartitionID k1,
                                     const OriginalHypergraphInfo& info,
                                     const double degree_of_parallism);

  void recursive_bipartitioning(PartitionedHypergraph& phg,
                                const Context& context,
                                const PartitionID k0, const PartitionID k1,
                                const OriginalHypergraphInfo& info) {
    // Multilevel Bipartitioning
    Hypergraph& hg = phg.hypergraph(); // previous codes makes here a copy, I do not know why...
    Context b_context = setupBipartitioningContext(hg, context, info);
    DBG << "Multilevel Bipartitioning - Range = (" << k0 << "," << k1 << "), Epsilon =" << b_context.partition.epsilon;
    PartitionedHypergraph bipartitioned_hg = multilevel::partition(hg, b_context);

    const PartitionID k = (k1 - k0);
    const PartitionID block_0 = 0;
    const PartitionID block_1 = k / 2 + (k % 2);
    phg.doParallelForAllNodes([&](const HypernodeID& hn) {
      PartitionID part_id = bipartitioned_hg.partID(hn);
      ASSERT(part_id != kInvalidPartition && part_id < phg.k());
      ASSERT(phg.partID(hn) == kInvalidPartition);
      if ( part_id == 0 ) {
        phg.setOnlyNodePart(hn, block_0);
      } else {
        phg.setOnlyNodePart(hn, block_1);
      }
    });
    phg.initializePartition();

    ASSERT(metrics::objective(bipartitioned_hg, context.partition.objective) ==
           metrics::objective(phg, context.partition.objective));

    ASSERT(context.partition.k >= 2);
    PartitionID rb_k0 = context.partition.k / 2 + context.partition.k % 2;
    PartitionID rb_k1 = context.partition.k / 2;
    if ( rb_k0 >= 2 && rb_k1 >= 2 ) {
      // In case we have to partition both blocks from the bisection further into
      // more than one block, we call the recursive bipartitioning algorithm
      // recursively in parallel
      DBG << "Current k = " << context.partition.k << "\n"
          << "Block" << block_0 << "is further partitioned into k =" << rb_k0 << "blocks\n"
          << "Block" << block_1 << "is further partitioned into k =" << rb_k1 << "blocks\n";
      tbb::task_group tg;
      tg.run([&] { recursively_bipartition_block(phg, context, block_0, 0, rb_k0, info, 0.5); });
      tg.run([&] { recursively_bipartition_block(phg, context, block_1, rb_k0, rb_k0 + rb_k1, info, 0.5); });
      tg.wait();
    } else if ( rb_k0 >= 2 ) {
      ASSERT(rb_k1 < 2);
      // In case only the first block has to be partitioned into more than one block, we call
      // the recursive bipartitioning algorithm recusively on the block 0
      DBG << "Current k = " << context.partition.k << "\n"
          << "Block" << block_0 << "is further partitioned into k =" << rb_k0 << "blocks\n";
      recursively_bipartition_block(phg, context,
        block_0, 0, rb_k0, info, 1.0);
    }
  }
}

void tmp::recursively_bipartition_block(PartitionedHypergraph& phg,
                                        const Context& context,
                                        const PartitionID block, const PartitionID k0, const PartitionID k1,
                                        const OriginalHypergraphInfo& info,
                                        const double degree_of_parallism) {
  Context rb_context = setupRecursiveBipartitioningContext(context, k0, k1, degree_of_parallism);
  // Extracts the block of the hypergraph which we recursively want to partition
  bool cut_net_splitting = context.partition.objective == Objective::km1;
  auto copy_hypergraph = phg.extract(block, cut_net_splitting,
    context.preprocessing.stable_construction_of_incident_edges);
  Hypergraph& rb_hg = copy_hypergraph.first;
  auto& mapping = copy_hypergraph.second;

  if ( rb_hg.initialNumNodes() > 0 ) {
    PartitionedHypergraph rb_phg(rb_context.partition.k, rb_hg, parallel_tag_t());
    recursive_bipartitioning(rb_phg, rb_context, k0, k1, info);

    ASSERT(phg.initialNumNodes() == mapping.size());
    phg.doParallelForAllNodes([&](const HypernodeID& hn) {
      if ( phg.partID(hn) == block ) {
        ASSERT(hn < mapping.size());
        PartitionID to = block + rb_phg.partID(mapping[hn]);
        ASSERT(to != kInvalidPartition && to < phg.k());
        if ( block != to ) {
          phg.changeNodePart(hn, block, to);
        }
      }
    });
  }
}

namespace recursive_bipartitioning {

  PartitionedHypergraph partition(Hypergraph& hypergraph, const Context& context) {
    PartitionedHypergraph partitioned_hypergraph(context.partition.k, hypergraph, parallel_tag_t());
    partition(partitioned_hypergraph, context);
    return partitioned_hypergraph;
  }

  void partition(PartitionedHypergraph& hypergraph, const Context& context) {
    utils::Utilities& utils = utils::Utilities::instance();
    if (context.partition.mode == Mode::recursive_bipartitioning) {
      utils.getTimer(context.utility_id).start_timer("rb", "Recursive Bipartitioning");
    }

    if (context.type == ContextType::main) {
      parallel::MemoryPool::instance().deactivate_unused_memory_allocations();
      utils.getTimer(context.utility_id).disable();
      utils.getStats(context.utility_id).disable();
    }

    // RecursiveMultilevelBipartitioningTask& root_bisection_task = *new(tbb::task::allocate_root()) RecursiveMultilevelBipartitioningTask(
    //         OriginalHypergraphInfo { hypergraph.totalWeight(), context.partition.k, context.partition.epsilon }, hypergraph, context);
    // tbb::task::spawn_root_and_wait(root_bisection_task);

    tmp::recursive_bipartitioning(hypergraph, context, 0, context.partition.k,
      OriginalHypergraphInfo { hypergraph.totalWeight(), context.partition.k, context.partition.epsilon });

    if (context.type == ContextType::main) {
      parallel::MemoryPool::instance().activate_unused_memory_allocations();
      utils.getTimer(context.utility_id).enable();
      utils.getStats(context.utility_id).enable();
    }
    if (context.partition.mode == Mode::recursive_bipartitioning) {
      utils.getTimer(context.utility_id).stop_timer("rb");
    }
  }
} // namespace recursive_bipartitioning
} // namepace mt_kahypar