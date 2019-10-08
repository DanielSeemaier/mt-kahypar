/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2019 Tobias Heuer <tobias.heuer@kit.edu>
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

#include <atomic>
#include <atomic>

#include "gmock/gmock.h"

#include "tests/datastructures/hypergraph_fixtures.h"

using ::testing::Test;

namespace mt_kahypar {
namespace ds {

using AConcurrentHypergraph = AHypergraph<2>;
using TestHypergraph = typename AConcurrentHypergraph::TestHypergraph;
using TestStreamingHypergraph = typename AConcurrentHypergraph::TestStreamingHypergraph;

void assignPartitionIDs(TestHypergraph& hypergraph) {
  hypergraph.setNodePart(hypergraph.globalNodeID(0), 0);
  hypergraph.setNodePart(hypergraph.globalNodeID(1), 0);
  hypergraph.setNodePart(hypergraph.globalNodeID(2), 0);
  hypergraph.setNodePart(hypergraph.globalNodeID(3), 1);
  hypergraph.setNodePart(hypergraph.globalNodeID(4), 1);
  hypergraph.setNodePart(hypergraph.globalNodeID(5), 2);
  hypergraph.setNodePart(hypergraph.globalNodeID(6), 2);
  hypergraph.updateGlobalPartInfos();
}

TestHypergraph construct_test_hypergraph(const AConcurrentHypergraph& test) {
  TestHypergraph hypergraph = test.construct_hypergraph(7,
                                { {0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6} },
                                { 0, 0, 0, 1, 1, 1, 1 },
                                { 0, 0, 1, 1 },
                                { 0, 0, 0, 1, 1, 2, 1 }, 3 );
  assignPartitionIDs(hypergraph);
  return hypergraph;
}

template< class F, class K >
void executeConcurrent( F f1, K f2 ) {
  std::atomic<int> cnt(0);
  tbb::task_group group;

  group.run([&] {
    cnt++;
    while ( cnt < 2 ) { }
    f1();
  });

  group.run([&] {
    cnt++;
    while ( cnt < 2 ) { }
    f2();
  });

  group.wait();
}

TEST_F(AConcurrentHypergraph, HasCorrectLocalPartWeights) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_EQ(3, hypergraph.localPartWeight(0));
    ASSERT_EQ(3, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(2, hypergraph.localPartWeight(2));
    ASSERT_EQ(2, hypergraph.localPartSize(2));
  }, [&] {
    ASSERT_EQ(3, hypergraph.localPartWeight(0));
    ASSERT_EQ(3, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(2, hypergraph.localPartWeight(2));
    ASSERT_EQ(2, hypergraph.localPartSize(2));
  });
}

TEST_F(AConcurrentHypergraph, HasCorrectLocalPartWeightsIfOnlyOneThreadPerformsModificationsBefore) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  hypergraph.changeNodePart(0, 0, 1);
  hypergraph.updateGlobalPartInfos();

  executeConcurrent([&] {
    ASSERT_EQ(2, hypergraph.localPartWeight(0));
    ASSERT_EQ(2, hypergraph.localPartSize(0));
    ASSERT_EQ(3, hypergraph.localPartWeight(1));
    ASSERT_EQ(3, hypergraph.localPartSize(1));
    ASSERT_EQ(2, hypergraph.localPartWeight(2));
    ASSERT_EQ(2, hypergraph.localPartSize(2));
  }, [&] {
    ASSERT_EQ(2, hypergraph.localPartWeight(0));
    ASSERT_EQ(2, hypergraph.localPartSize(0));
    ASSERT_EQ(3, hypergraph.localPartWeight(1));
    ASSERT_EQ(3, hypergraph.localPartSize(1));
    ASSERT_EQ(2, hypergraph.localPartWeight(2));
    ASSERT_EQ(2, hypergraph.localPartSize(2));
  });
}

TEST_F(AConcurrentHypergraph, PerformsTwoConcurrentMovesWhereOnlyOneSucceeds) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  std::array<bool, 2> success;
  executeConcurrent([&] {
    success[0] = hypergraph.changeNodePart(0, 0, 1);

    if ( success[0] ) {
      ASSERT_EQ(2, hypergraph.localPartWeight(0));
      ASSERT_EQ(2, hypergraph.localPartSize(0));
      ASSERT_EQ(3, hypergraph.localPartWeight(1));
      ASSERT_EQ(3, hypergraph.localPartSize(1));
    }

  }, [&] {
    success[1] = hypergraph.changeNodePart(0, 0, 2);

    if ( success[1] ) {
      ASSERT_EQ(2, hypergraph.localPartWeight(0));
      ASSERT_EQ(2, hypergraph.localPartSize(0));
      ASSERT_EQ(3, hypergraph.localPartWeight(2));
      ASSERT_EQ(3, hypergraph.localPartSize(2));
    }
  });

  ASSERT_TRUE( ( success[0] && !success[1] ) || ( !success[0] && success[1] ) );

  hypergraph.updateGlobalPartInfos();
  ASSERT_EQ(2, hypergraph.partWeight(0));
  ASSERT_EQ(2, hypergraph.partSize(0));
  if ( success[0] ) {
    ASSERT_EQ(3, hypergraph.partWeight(1));
    ASSERT_EQ(3, hypergraph.partSize(1));
    ASSERT_EQ(2, hypergraph.partWeight(2));
    ASSERT_EQ(2, hypergraph.partSize(2));
  } else {
    ASSERT_EQ(2, hypergraph.partWeight(1));
    ASSERT_EQ(2, hypergraph.partSize(1));
    ASSERT_EQ(3, hypergraph.partWeight(2));
    ASSERT_EQ(3, hypergraph.partSize(2));
  }
}

TEST_F(AConcurrentHypergraph, PerformsConcurrentMovesWhereAllSucceed) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(0), 0, 1) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(3), 1, 2) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(2), 0, 2) );

    ASSERT_EQ(1, hypergraph.localPartWeight(0));
    ASSERT_EQ(1, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(4, hypergraph.localPartWeight(2));
    ASSERT_EQ(4, hypergraph.localPartSize(2));
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(5), 2, 1) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(6), 2, 0) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(4), 1, 2) );

    ASSERT_EQ(4, hypergraph.localPartWeight(0));
    ASSERT_EQ(4, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(1, hypergraph.localPartWeight(2));
    ASSERT_EQ(1, hypergraph.localPartSize(2));
  });

  hypergraph.updateGlobalPartInfos();
  ASSERT_EQ(2, hypergraph.partWeight(0));
  ASSERT_EQ(2, hypergraph.partSize(0));
  ASSERT_EQ(2, hypergraph.partWeight(1));
  ASSERT_EQ(2, hypergraph.partSize(1));
  ASSERT_EQ(3, hypergraph.partWeight(2));
  ASSERT_EQ(3, hypergraph.partSize(2));
}

TEST_F(AConcurrentHypergraph, PerformsConcurrentMovesAndUpdatesLocalPartInfos) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  std::atomic<size_t> cnt(0);
  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(0), 0, 1) ); // Move 1

    cnt++;
    while( cnt < 3 ) { }

    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(3), 1, 2) ); // Move 5

    hypergraph.updateLocalPartInfos(); // Move 1, 2, 3, 4, 5 are applied
    ASSERT_EQ(3, hypergraph.localPartWeight(0));
    ASSERT_EQ(3, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(2, hypergraph.localPartWeight(2));
    ASSERT_EQ(2, hypergraph.localPartSize(2));

    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(2), 0, 2) ); // Move 6

    ASSERT_EQ(2, hypergraph.localPartWeight(0));
    ASSERT_EQ(2, hypergraph.localPartSize(0));
    ASSERT_EQ(2, hypergraph.localPartWeight(1));
    ASSERT_EQ(2, hypergraph.localPartSize(1));
    ASSERT_EQ(3, hypergraph.localPartWeight(2));
    ASSERT_EQ(3, hypergraph.localPartSize(2));
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(5), 2, 1) ); // Move 2
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(6), 2, 0) ); // Move 3

    cnt++;
    while ( cnt < 2 ) { }

    hypergraph.updateLocalPartInfos(); // Move 1, 2, 3 are applied
    ASSERT_EQ(3, hypergraph.localPartWeight(0));
    ASSERT_EQ(3, hypergraph.localPartSize(0));
    ASSERT_EQ(4, hypergraph.localPartWeight(1));
    ASSERT_EQ(4, hypergraph.localPartSize(1));
    ASSERT_EQ(0, hypergraph.localPartWeight(2));
    ASSERT_EQ(0, hypergraph.localPartSize(2));

    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(4), 1, 2) ); // Move 4

    cnt++;

    ASSERT_EQ(3, hypergraph.localPartWeight(0));
    ASSERT_EQ(3, hypergraph.localPartSize(0));
    ASSERT_EQ(3, hypergraph.localPartWeight(1));
    ASSERT_EQ(3, hypergraph.localPartSize(1));
    ASSERT_EQ(1, hypergraph.localPartWeight(2));
    ASSERT_EQ(1, hypergraph.localPartSize(2));
  });

  hypergraph.updateGlobalPartInfos();
  ASSERT_EQ(2, hypergraph.partWeight(0));
  ASSERT_EQ(2, hypergraph.partSize(0));
  ASSERT_EQ(2, hypergraph.partWeight(1));
  ASSERT_EQ(2, hypergraph.partSize(1));
  ASSERT_EQ(3, hypergraph.partWeight(2));
  ASSERT_EQ(3, hypergraph.partSize(2));
}

void verifyPartitionPinCounts(TestHypergraph& hypergraph,
                              const HyperedgeID he,
                              const std::vector<HypernodeID>& expected_pin_counts) {
  ASSERT(expected_pin_counts.size() == 3);
  for ( PartitionID k = 0; k < 3; ++k ) {
    ASSERT_EQ(expected_pin_counts[k], hypergraph.pinCountInPart(he, k)) << V(he) << V(k);
  }
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCounts) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {2, 0, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {2, 2, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {0, 2, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {1, 0, 2});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfTwoNodesMovesConcurrent1) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(0), 0, 1) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(1), 0, 2) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {1, 1, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {0, 3, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {0, 2, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {1, 0, 2});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfTwoNodesMovesConcurrent2) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(3), 1, 2) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(6), 2, 0) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {2, 0, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {2, 1, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {1, 1, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {2, 0, 1});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfTwoNodesMovesConcurrent3) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(3), 1, 2) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(4), 1, 2) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {2, 0, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {2, 0, 2});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {0, 0, 3});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {1, 0, 2});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfTwoNodesMovesConcurrent4) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(2), 0, 2) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(5), 2, 0) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {1, 0, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {2, 2, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {0, 2, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {1, 0, 2});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfTwoNodesMovesConcurrent5) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(0), 0, 1) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(6), 2, 1) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {1, 1, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {1, 3, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {0, 3, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {1, 1, 1});
}

TEST_F(AConcurrentHypergraph, HasCorrectPartitionPinCountsIfAllNodesMovesConcurrent) {
  TestHypergraph hypergraph = construct_test_hypergraph(*this);

  executeConcurrent([&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(0), 0, 1) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(2), 0, 2) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(4), 1, 0) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(6), 2, 1) );
  }, [&] {
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(1), 0, 2) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(3), 1, 0) );
    ASSERT_TRUE( hypergraph.changeNodePart(hypergraph.globalNodeID(5), 2, 1) );
  });

  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(0), {0, 1, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(1), {2, 1, 1});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(2), {2, 1, 0});
  verifyPartitionPinCounts(hypergraph, hypergraph.globalEdgeID(3), {0, 2, 1});
}

} // namespace ds
} // namespace mt_kahypar