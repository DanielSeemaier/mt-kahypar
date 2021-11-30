/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2020 Lars Gottesbüren <lars.gottesbueren@kit.edu>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include "gmock/gmock.h"

#include "tests/partition/refinement/flow_refiner_mock.h"
#include "mt-kahypar/partition/refinement/flows/refiner_adapter.h"

using ::testing::Test;

#define MOVE(HN, FROM, TO) Move { FROM, TO, HN, 0 }

namespace mt_kahypar {

class AFlowRefinerAdapter : public Test {
 public:
  AFlowRefinerAdapter() :
    hg(HypergraphFactory::construct(7 , 4, {
       {0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6} }, nullptr, nullptr, true)),
    phg(2, hg, parallel_tag_t()),
    context() {
    context.partition.k = 2;
    context.partition.perfect_balance_part_weights.assign(2, 3);
    context.partition.max_part_weights.assign(2, 4);
    context.partition.objective = kahypar::Objective::km1;
    context.shared_memory.num_threads = 8;
    context.refinement.flows.algorithm = FlowAlgorithm::mock;
    context.refinement.flows.num_parallel_searches = std::thread::hardware_concurrency();

    FlowRefinerMockControl::instance().reset();

    phg.setOnlyNodePart(0, 0);
    phg.setOnlyNodePart(1, 0);
    phg.setOnlyNodePart(2, 0);
    phg.setOnlyNodePart(3, 0);
    phg.setOnlyNodePart(4, 1);
    phg.setOnlyNodePart(5, 1);
    phg.setOnlyNodePart(6, 1);
    phg.initializePartition();
  }

  Hypergraph hg;
  PartitionedHypergraph phg;
  Context context;
  std::unique_ptr<FlowRefinerAdapter> refiner;
};

template <class F, class K>
void executeConcurrent(F f1, K f2) {
  std::atomic<int> cnt(0);

  tbb::parallel_invoke([&] {
    cnt++;
    while (cnt < 2) { }
    f1();
  }, [&] {
    cnt++;
    while (cnt < 2) { }
    f2();
  });
}

TEST_F(AFlowRefinerAdapter, FailsToRegisterMoreSearchesIfAllAreUsed) {
  context.shared_memory.num_threads = std::thread::hardware_concurrency();
  context.refinement.flows.num_parallel_searches = std::thread::hardware_concurrency() / 2;
  refiner = std::make_unique<FlowRefinerAdapter>(hg, context);

  for ( size_t i = 0; i < context.refinement.flows.num_parallel_searches; ++i ) {
    ASSERT_TRUE(refiner->registerNewSearch(i, phg));
  }
  ASSERT_FALSE(refiner->registerNewSearch(context.refinement.flows.num_parallel_searches, phg));
}

TEST_F(AFlowRefinerAdapter, UseCorrectNumberOfThreadsForSearch1) {
  context.shared_memory.num_threads = std::thread::hardware_concurrency();
  context.refinement.flows.num_parallel_searches = std::thread::hardware_concurrency() / 2;
  const size_t num_threads_per_search = std::max(1UL,
    static_cast<size_t>(std::ceil(static_cast<double>(
      context.shared_memory.num_threads) /
      context.refinement.flows.num_parallel_searches)));
  refiner = std::make_unique<FlowRefinerAdapter>(hg, context);
  ASSERT_EQ(context.refinement.flows.num_parallel_searches, refiner->numAvailableRefiner());
  ASSERT_EQ(0, refiner->numUsedThreads());

  FlowRefinerMockControl::instance().refine_func =
    [&](const PartitionedHypergraph&, const Subhypergraph&, const size_t num_threads) -> MoveSequence {
      EXPECT_EQ(num_threads_per_search, num_threads);
      EXPECT_EQ(num_threads_per_search, refiner->numUsedThreads());
      return MoveSequence { {}, 0 };
    };
  ASSERT_TRUE(refiner->registerNewSearch(0, phg));
  refiner->refine(0, phg, {});
  refiner->finalizeSearch(0);
}

TEST_F(AFlowRefinerAdapter, UseCorrectNumberOfThreadsForSearch2) {
  context.shared_memory.num_threads = std::thread::hardware_concurrency();
  context.refinement.flows.num_parallel_searches = std::thread::hardware_concurrency() / 2;
  const size_t num_threads_per_search = std::max(1UL,
    static_cast<size_t>(std::ceil(static_cast<double>(
      context.shared_memory.num_threads) /
      context.refinement.flows.num_parallel_searches)));
  refiner = std::make_unique<FlowRefinerAdapter>(hg, context);
  ASSERT_EQ(context.refinement.flows.num_parallel_searches, refiner->numAvailableRefiner());
  ASSERT_EQ(0, refiner->numUsedThreads());

  std::atomic<size_t> cnt(0);
  FlowRefinerMockControl::instance().refine_func =
    [&](const PartitionedHypergraph&, const Subhypergraph&, const size_t num_threads) -> MoveSequence {
      EXPECT_EQ(num_threads_per_search, num_threads);
      EXPECT_EQ(num_threads_per_search, refiner->numUsedThreads());
      ++cnt;
      while ( cnt < 2 ) { }
      return MoveSequence { {}, 0 };
    };
  ASSERT_TRUE(refiner->registerNewSearch(0, phg));
  FlowRefinerMockControl::instance().refine_func =
    [&](const PartitionedHypergraph&, const Subhypergraph&, const size_t num_threads) -> MoveSequence {
      EXPECT_EQ(num_threads_per_search, num_threads);
      EXPECT_EQ(2 * num_threads_per_search, refiner->numUsedThreads());
      ++cnt;
      return MoveSequence { {}, 0 };
    };
  ASSERT_TRUE(refiner->registerNewSearch(1, phg));
  executeConcurrent([&] {
    refiner->refine(0, phg, {});
  }, [&] {
    while ( cnt < 1 ) { }
    refiner->refine(1, phg, {});
  });
  refiner->finalizeSearch(0);
  refiner->finalizeSearch(1);
}

}