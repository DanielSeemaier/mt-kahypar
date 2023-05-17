/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2023 Tobias Heuer <tobias.heuer@kit.edu>
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

#pragma once

#include <queue>

#include "tbb/enumerable_thread_specific.h"

#include "mt-kahypar/macros.h"
#include "mt-kahypar/datastructures/static_graph.h"
#include "mt-kahypar/datastructures/static_bitset.h"

namespace mt_kahypar {

class ProcessGraph {

  static constexpr size_t MEMORY_LIMIT = 100000000;

  using PQElement = std::pair<HyperedgeWeight, PartitionID>;
  using PQ = std::priority_queue<PQElement, vec<PQElement>, std::greater<PQElement>>;

  struct MSTData {
    MSTData(const size_t n) :
      bitset(n),
      lightest_edge(n),
      pq() { }

    ds::Bitset bitset;
    vec<HyperedgeWeight> lightest_edge;
    PQ pq;
  };

 public:
  explicit ProcessGraph(ds::StaticGraph&& graph) :
    _is_initialized(true),
    _k(graph.initialNumNodes()),
    _graph(std::move(graph)),
    _max_precomputed_connectitivty(0),
    _distances(),
    _local_mst_data(graph.initialNumNodes()) { }

  ProcessGraph(const ProcessGraph&) = delete;
  ProcessGraph & operator= (const ProcessGraph &) = delete;

  ProcessGraph(ProcessGraph&&) = default;
  ProcessGraph & operator= (ProcessGraph &&) = default;

  PartitionID numBlocks() const {
    return _k;
  }

  // ! This function computes the weight of all steiner trees for all
  // ! connectivity sets with connectivity at most m (:= max_connectivity),
  void precomputeDistances(const size_t max_conectivity);

  // ! Returns the weight of the optimal steiner tree between all blocks
  // ! in the connectivity set if precomputed. Otherwise, we compute
  // ! a 2-approximation of the optimal steiner tree
  // ! (see computeWeightOfMSTOnMetricCompletion(...))
  HyperedgeWeight distance(const ds::StaticBitset& connectivity_set);

  // ! Returns the shortest path between two blocks in the process graph
  HyperedgeWeight distance(const PartitionID i, const PartitionID j) {
    ASSERT(_is_initialized);
    return _distances[index(i, j)];
  }

 private:
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE size_t index(const PartitionID i,
                                                  const PartitionID j) {
    ASSERT(i < _k && j < _k);
    return i + j * _k;
  }

  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE size_t index(const ds::StaticBitset& connectivity_set) {
    size_t index = 0;
    PartitionID multiplier = 1;
    PartitionID last_block = kInvalidPartition;
    for ( const PartitionID block : connectivity_set ) {
      index += multiplier * block;
      multiplier *= _k;
      last_block = block;
    }
    return index + (multiplier == _k ? last_block * _k : 0);
  }

  // ! This function computes an MST on the metric completion of the process graph
  // ! restricted to the blocks in the connectivity set. The metric completion is
  // ! complete graph where each edge {u,v} has a weight equals the shortest path
  // ! connecting u and v. This gives a 2-approximation for steiner tree problem.
  HyperedgeWeight computeWeightOfMSTOnMetricCompletion(const ds::StaticBitset& connectivity_set);

  bool _is_initialized;
  PartitionID _k;
  ds::StaticGraph _graph;
  PartitionID _max_precomputed_connectitivty;
  vec<HyperedgeWeight> _distances;
  tbb::enumerable_thread_specific<MSTData> _local_mst_data;
};

}  // namespace kahypar
