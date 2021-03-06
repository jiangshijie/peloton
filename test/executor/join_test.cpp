//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// join_test.cpp
//
// Identification: test/executor/join_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "common/harness.h"

#include "common/types.h"
#include "executor/logical_tile.h"
#include "executor/logical_tile_factory.h"

#include "executor/hash_join_executor.h"
#include "executor/hash_executor.h"
#include "executor/index_scan_executor.h"
#include "executor/merge_join_executor.h"
#include "executor/nested_loop_join_executor.h"

#include "expression/abstract_expression.h"
#include "expression/tuple_value_expression.h"
#include "expression/expression_util.h"

#include "planner/hash_join_plan.h"
#include "planner/hash_plan.h"
#include "planner/merge_join_plan.h"
#include "planner/nested_loop_join_plan.h"

#include "storage/data_table.h"
#include "storage/tile.h"

#include "concurrency/transaction_manager_factory.h"

#include "executor/mock_executor.h"
#include "executor/executor_tests_util.h"
#include "executor/join_tests_util.h"

using ::testing::NotNull;
using ::testing::Return;
using ::testing::InSequence;

namespace peloton {
namespace test {

class JoinTests : public PelotonTest {};

std::vector<planner::MergeJoinPlan::JoinClause> CreateJoinClauses() {
  std::vector<planner::MergeJoinPlan::JoinClause> join_clauses;
  auto left = expression::ExpressionUtil::TupleValueFactory(
      common::Type::INTEGER, 0, 1);
  auto right = expression::ExpressionUtil::TupleValueFactory(
      common::Type::INTEGER, 1, 1);
  bool reversed = false;
  join_clauses.emplace_back(left, right, reversed);
  return join_clauses;
}

std::shared_ptr<const peloton::catalog::Schema> CreateJoinSchema() {
  return std::shared_ptr<const peloton::catalog::Schema>(
      new catalog::Schema({ExecutorTestsUtil::GetColumnInfo(1),
                           ExecutorTestsUtil::GetColumnInfo(1),
                           ExecutorTestsUtil::GetColumnInfo(0),
                           ExecutorTestsUtil::GetColumnInfo(0)}));
}

// PLAN_NODE_TYPE_NESTLOOP is picked out as a separated test
std::vector<PlanNodeType> join_algorithms = {PLAN_NODE_TYPE_MERGEJOIN,
                                             PLAN_NODE_TYPE_HASHJOIN};

std::vector<PelotonJoinType> join_types = {JOIN_TYPE_INNER, JOIN_TYPE_LEFT,
                                           JOIN_TYPE_RIGHT, JOIN_TYPE_OUTER};

void ExecuteJoinTest(PlanNodeType join_algorithm, PelotonJoinType join_type,
                     oid_t join_test_type);
void ExecuteNestedLoopJoinTest(PelotonJoinType join_type);

void PopulateTable(storage::DataTable *table, int num_rows, bool random,
                   concurrency::Transaction *current_txn);

oid_t CountTuplesWithNullFields(executor::LogicalTile *logical_tile);

void ValidateJoinLogicalTile(executor::LogicalTile *logical_tile);
void ValidateNestedLoopJoinLogicalTile(executor::LogicalTile *logical_tile);

void ExpectEmptyTileResult(MockExecutor *table_scan_executor);

void ExpectMoreThanOneTileResults(
    MockExecutor *table_scan_executor,
    std::vector<std::unique_ptr<executor::LogicalTile>> &
        table_logical_tile_ptrs);

void ExpectNormalTileResults(
    size_t table_tile_group_count, MockExecutor *table_scan_executor,
    std::vector<std::unique_ptr<executor::LogicalTile>> &
        table_logical_tile_ptrs);

enum JOIN_TEST_TYPE {
  BASIC_TEST = 0,
  BOTH_TABLES_EMPTY = 1,
  COMPLICATED_TEST = 2,
  SPEED_TEST = 3,
  LEFT_TABLE_EMPTY = 4,
  RIGHT_TABLE_EMPTY = 5,
};

TEST_F(JoinTests, BasicTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    ExecuteJoinTest(join_algorithm, JOIN_TYPE_INNER, BASIC_TEST);
  }
}

TEST_F(JoinTests, EmptyTablesTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    ExecuteJoinTest(join_algorithm, JOIN_TYPE_INNER, BOTH_TABLES_EMPTY);
  }
}

TEST_F(JoinTests, JoinTypesTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    // Go over all join types
    for (auto join_type : join_types) {
      LOG_INFO("JOIN TYPE :: %d", join_type);
      // Execute the join test
      ExecuteJoinTest(join_algorithm, join_type, BASIC_TEST);
    }
  }
}

TEST_F(JoinTests, ComplicatedTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    // Go over all join types
    for (auto join_type : join_types) {
      LOG_INFO("JOIN TYPE :: %d", join_type);
      // Execute the join test
      ExecuteJoinTest(join_algorithm, join_type, COMPLICATED_TEST);
    }
  }
}

TEST_F(JoinTests, LeftTableEmptyTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    // Go over all join types
    for (auto join_type : join_types) {
      LOG_INFO("JOIN TYPE :: %d", join_type);
      // Execute the join test
      ExecuteJoinTest(join_algorithm, join_type, LEFT_TABLE_EMPTY);
    }
  }
}

TEST_F(JoinTests, RightTableEmptyTest) {
  // Go over all join algorithms
  for (auto join_algorithm : join_algorithms) {
    LOG_INFO("JOIN ALGORITHM :: %s",
             PlanNodeTypeToString(join_algorithm).c_str());
    // Go over all join types
    for (auto join_type : join_types) {
      LOG_INFO("JOIN TYPE :: %d", join_type);
      // Execute the join test
      ExecuteJoinTest(join_algorithm, join_type, RIGHT_TABLE_EMPTY);
    }
  }
}

TEST_F(JoinTests, JoinPredicateTest) {
  oid_t join_test_types = 1;

  // Go over all join test types
  for (oid_t join_test_type = 0; join_test_type < join_test_types;
       join_test_type++) {
    LOG_INFO("JOIN TEST_F ------------------------ :: %u", join_test_type);

    // Go over all join algorithms
    for (auto join_algorithm : join_algorithms) {
      LOG_INFO("JOIN ALGORITHM :: %s",
               PlanNodeTypeToString(join_algorithm).c_str());
      // Go over all join types
      for (auto join_type : join_types) {
        LOG_INFO("JOIN TYPE :: %d", join_type);
        // Execute the join test
        ExecuteJoinTest(join_algorithm, join_type, join_test_type);
      }
    }
  }
}

TEST_F(JoinTests, SpeedTest) {
  ExecuteJoinTest(PLAN_NODE_TYPE_HASHJOIN, JOIN_TYPE_OUTER, SPEED_TEST);

  ExecuteJoinTest(PLAN_NODE_TYPE_MERGEJOIN, JOIN_TYPE_OUTER, SPEED_TEST);

  ExecuteNestedLoopJoinTest(JOIN_TYPE_OUTER);
}

TEST_F(JoinTests, BasicNestedLoopTest) {
  LOG_INFO("PLAN_NODE_TYPE_NESTLOOP");
  ExecuteNestedLoopJoinTest(JOIN_TYPE_INNER);
}

void PopulateTable(storage::DataTable *table, int num_rows, bool random,
                   concurrency::Transaction *current_txn) {
  // Random values
  if (random) std::srand(std::time(nullptr));

  const catalog::Schema *schema = table->GetSchema();

  // Ensure that the tile group is as expected.
  PL_ASSERT(schema->GetColumnCount() == 4);

  // Insert tuples into tile_group.
  const bool allocate = true;
  auto testing_pool = TestingHarness::GetInstance().GetTestingPool();
  for (int rowid = 0; rowid < num_rows; rowid++) {

    storage::Tuple tuple(schema, allocate);

    // First column is unique in this case
    tuple.SetValue(0, common::ValueFactory::GetIntegerValue(50 * rowid).Copy(),
                   testing_pool);

    // In case of random, make sure this column has duplicated values
    tuple.SetValue(1,
                   common::ValueFactory::GetIntegerValue(50 * rowid * 2).Copy(),
                   testing_pool);

    tuple.SetValue(2, common::ValueFactory::GetDoubleValue(1.5).Copy(),
                   testing_pool);

    // In case of random, make sure this column has duplicated values
    auto string_value =
        common::ValueFactory::GetVarcharValue(std::to_string(123));
    tuple.SetValue(3, string_value, testing_pool);

    ItemPointer *index_entry_ptr = nullptr;
    ItemPointer tuple_slot_id =
        table->InsertTuple(&tuple, current_txn, &index_entry_ptr);
    PL_ASSERT(tuple_slot_id.block != INVALID_OID);
    PL_ASSERT(tuple_slot_id.offset != INVALID_OID);

    auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
    txn_manager.PerformInsert(current_txn, tuple_slot_id, index_entry_ptr);
  }
}

void ExecuteNestedLoopJoinTest(PelotonJoinType join_type) {
  //===--------------------------------------------------------------------===//
  // Create Table
  //===--------------------------------------------------------------------===//

  // MockExecutor left_table_scan_executor, right_table_scan_executor;

  // Create a table and wrap it in logical tile
  size_t tile_group_size = TESTS_TUPLES_PER_TILEGROUP;
  size_t left_table_tile_group_count = 3;
  size_t right_table_tile_group_count = 2;

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();

  // Left table has 3 tile groups (15 tuples)
  std::unique_ptr<storage::DataTable> left_table(
      ExecutorTestsUtil::CreateTable(tile_group_size));
  ExecutorTestsUtil::PopulateTable(
      left_table.get(), tile_group_size * left_table_tile_group_count, false,
      false, false, txn);

  // Right table has 2 tile groups (10 tuples)
  std::unique_ptr<storage::DataTable> right_table(
      ExecutorTestsUtil::CreateTable(tile_group_size));
  PopulateTable(right_table.get(),
                tile_group_size * right_table_tile_group_count, false, txn);

  txn_manager.CommitTransaction(txn);

  left_table->PrintTable();
  right_table->PrintTable();

  //===--------------------------------------------------------------------===//
  // Begin nested loop
  //===--------------------------------------------------------------------===//
  txn = txn_manager.BeginTransaction();
  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  //===--------------------------------------------------------------------===//
  // Create executors
  //===--------------------------------------------------------------------===//
  //  executor::IndexScanExecutor left_table_scan_executor,
  //      right_table_scan_executor;

  // LEFT ATTR 0 == 100
  auto index = left_table->GetIndex(0);
  std::vector<oid_t> key_column_ids;
  std::vector<ExpressionType> expr_types;
  std::vector<common::Value> values;
  std::vector<expression::AbstractExpression *> runtime_keys;

  key_column_ids.push_back(0);
  expr_types.push_back(ExpressionType::EXPRESSION_TYPE_COMPARE_EQUAL);
  values.push_back(common::ValueFactory::GetIntegerValue(50).Copy());

  // Create index scan desc
  planner::IndexScanPlan::IndexScanDesc index_scan_desc(
      index, key_column_ids, expr_types, values, runtime_keys);

  expression::AbstractExpression *predicate_scan = nullptr;
  std::vector<oid_t> column_ids({0, 1, 3});

  // Create plan node.
  planner::IndexScanPlan left_table_node(left_table.get(), predicate_scan,
                                         column_ids, index_scan_desc);

  // executor
  executor::IndexScanExecutor left_table_scan_executor(&left_table_node,
                                                       context.get());

  // Right ATTR 0 =
  auto index_right = right_table->GetIndex(0);
  std::vector<oid_t> key_column_ids_right;
  std::vector<ExpressionType> expr_types_right;
  std::vector<common::Value> values_right;
  std::vector<expression::AbstractExpression *> runtime_keys_right;

  key_column_ids_right.push_back(0);
  expr_types_right.push_back(ExpressionType::EXPRESSION_TYPE_COMPARE_EQUAL);
  values_right.push_back(common::ValueFactory::GetIntegerValue(50).Copy());

  // Create index scan desc
  planner::IndexScanPlan::IndexScanDesc index_scan_desc_right(
      index_right, key_column_ids_right, expr_types_right, values_right,
      runtime_keys_right);

  expression::AbstractExpression *predicate_scan_right = nullptr;
  std::vector<oid_t> column_ids_right({0, 1});

  // Create plan node.
  planner::IndexScanPlan right_table_node(
      right_table.get(), predicate_scan_right, column_ids_right,
      index_scan_desc_right);

  // executor
  executor::IndexScanExecutor right_table_scan_executor(&right_table_node,
                                                        context.get());

  //===--------------------------------------------------------------------===//
  // Setup join plan nodes and executors and run them
  //===--------------------------------------------------------------------===//

  oid_t result_tuple_count = 0;
  oid_t tuples_with_null = 0;

  auto projection = JoinTestsUtil::CreateProjection();
  // setup the projection schema
  auto schema = CreateJoinSchema();

  // Construct predicate
  expression::TupleValueExpression *left_table_attr_1 =
      new expression::TupleValueExpression(common::Type::INTEGER, 0, 0);
  expression::TupleValueExpression *right_table_attr_1 =
      new expression::TupleValueExpression(common::Type::INTEGER, 1, 0);

  std::unique_ptr<const expression::AbstractExpression> predicate(
      new expression::ComparisonExpression(EXPRESSION_TYPE_COMPARE_EQUAL,
                                           left_table_attr_1,
                                           right_table_attr_1));

  // Differ based on join algorithm
  // Create nested loop join plan node.
  planner::NestedLoopJoinPlan nested_loop_join_node(
      join_type, std::move(predicate), std::move(projection), schema);

  // Run the nested loop join executor
  executor::NestedLoopJoinExecutor nested_loop_join_executor(
      &nested_loop_join_node, context.get());

  // Construct the executor tree
  nested_loop_join_executor.AddChild(&left_table_scan_executor);
  nested_loop_join_executor.AddChild(&right_table_scan_executor);

  // Run the nested loop join executor
  EXPECT_TRUE(nested_loop_join_executor.Init());
  while (nested_loop_join_executor.Execute() == true) {
    std::unique_ptr<executor::LogicalTile> result_logical_tile(
        nested_loop_join_executor.GetOutput());

    if (result_logical_tile != nullptr) {
      result_tuple_count += result_logical_tile->GetTupleCount();
      tuples_with_null += CountTuplesWithNullFields(result_logical_tile.get());
      ValidateNestedLoopJoinLogicalTile(result_logical_tile.get());
      LOG_INFO("result tile info: %s", result_logical_tile->GetInfo().c_str());
      LOG_INFO("result_tuple_count: %u", result_tuple_count);
      LOG_INFO("tuples_with_null: %u", tuples_with_null);
    }
  }

  txn_manager.CommitTransaction(txn);
}

void ExecuteJoinTest(PlanNodeType join_algorithm, PelotonJoinType join_type,
                     oid_t join_test_type) {
  //===--------------------------------------------------------------------===//
  // Mock table scan executors
  //===--------------------------------------------------------------------===//

  MockExecutor left_table_scan_executor, right_table_scan_executor;

  // Create a table and wrap it in logical tile
  size_t tile_group_size = TESTS_TUPLES_PER_TILEGROUP;
  size_t left_table_tile_group_count = 3;
  size_t right_table_tile_group_count = 2;

  auto &txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto txn = txn_manager.BeginTransaction();

  // Left table has 3 tile groups (15 tuples)
  std::unique_ptr<storage::DataTable> left_table(
      ExecutorTestsUtil::CreateTable(tile_group_size));
  ExecutorTestsUtil::PopulateTable(
      left_table.get(), tile_group_size * left_table_tile_group_count, false,
      false, false, txn);

  // Right table has 2 tile groups (10 tuples)
  std::unique_ptr<storage::DataTable> right_table(
      ExecutorTestsUtil::CreateTable(tile_group_size));
  ExecutorTestsUtil::PopulateTable(
      right_table.get(), tile_group_size * right_table_tile_group_count, false,
      false, false, txn);

  txn_manager.CommitTransaction(txn);

  LOG_TRACE("%s", left_table->GetInfo().c_str());
  LOG_TRACE("%s", right_table->GetInfo().c_str());

  if (join_test_type == COMPLICATED_TEST) {
    // Modify some values in left and right tables for complicated test
    auto left_source_tile = left_table->GetTileGroup(2)->GetTile(0);
    auto right_dest_tile = right_table->GetTileGroup(1)->GetTile(0);
    auto right_source_tile = left_table->GetTileGroup(0)->GetTile(0);

    auto source_tile_tuple_count = left_source_tile->GetAllocatedTupleCount();
    auto source_tile_column_count = left_source_tile->GetColumnCount();

    // LEFT - 3 rd tile --> RIGHT - 2 nd tile
    for (oid_t tuple_itr = 3; tuple_itr < source_tile_tuple_count;
         tuple_itr++) {
      for (oid_t col_itr = 0; col_itr < source_tile_column_count; col_itr++) {
        common::Value val = (left_source_tile->GetValue(tuple_itr, col_itr));
        right_dest_tile->SetValue(val, tuple_itr, col_itr);
      }
    }

    // RIGHT - 1 st tile --> RIGHT - 2 nd tile
    // RIGHT - 2 nd tile --> RIGHT - 2 nd tile
    for (oid_t col_itr = 0; col_itr < source_tile_column_count; col_itr++) {
      common::Value val1 = (right_source_tile->GetValue(4, col_itr));
      right_dest_tile->SetValue(val1, 0, col_itr);
      common::Value val2 = (right_dest_tile->GetValue(3, col_itr));
      right_dest_tile->SetValue(val2, 2, col_itr);
    }
  }

  std::vector<std::unique_ptr<executor::LogicalTile>>
      left_table_logical_tile_ptrs;
  std::vector<std::unique_ptr<executor::LogicalTile>>
      right_table_logical_tile_ptrs;

  // Wrap the input tables with logical tiles
  for (size_t left_table_tile_group_itr = 0;
       left_table_tile_group_itr < left_table_tile_group_count;
       left_table_tile_group_itr++) {
    std::unique_ptr<executor::LogicalTile> left_table_logical_tile(
        executor::LogicalTileFactory::WrapTileGroup(
            left_table->GetTileGroup(left_table_tile_group_itr)));
    left_table_logical_tile_ptrs.push_back(std::move(left_table_logical_tile));
  }

  for (size_t right_table_tile_group_itr = 0;
       right_table_tile_group_itr < right_table_tile_group_count;
       right_table_tile_group_itr++) {
    std::unique_ptr<executor::LogicalTile> right_table_logical_tile(
        executor::LogicalTileFactory::WrapTileGroup(
            right_table->GetTileGroup(right_table_tile_group_itr)));
    right_table_logical_tile_ptrs.push_back(
        std::move(right_table_logical_tile));
  }

  // Left scan executor returns logical tiles from the left table

  EXPECT_CALL(left_table_scan_executor, DInit()).WillOnce(Return(true));

  //===--------------------------------------------------------------------===//
  // Setup left table
  //===--------------------------------------------------------------------===//
  if (join_test_type == BASIC_TEST || join_test_type == COMPLICATED_TEST ||
      join_test_type == SPEED_TEST) {
    ExpectNormalTileResults(left_table_tile_group_count,
                            &left_table_scan_executor,
                            left_table_logical_tile_ptrs);

  } else if (join_test_type == BOTH_TABLES_EMPTY) {
    ExpectEmptyTileResult(&left_table_scan_executor);
  } else if (join_test_type == LEFT_TABLE_EMPTY) {
    ExpectEmptyTileResult(&left_table_scan_executor);
  } else if (join_test_type == RIGHT_TABLE_EMPTY) {
    if (join_type == JOIN_TYPE_INNER || join_type == JOIN_TYPE_RIGHT) {
      ExpectMoreThanOneTileResults(&left_table_scan_executor,
                                   left_table_logical_tile_ptrs);
    } else {
      ExpectNormalTileResults(left_table_tile_group_count,
                              &left_table_scan_executor,
                              left_table_logical_tile_ptrs);
    }
  }

  // Right scan executor returns logical tiles from the right table

  // EXPECT_CALL(right_table_scan_executor, DInit()).WillOnce(Return(true));
  EXPECT_CALL(right_table_scan_executor, DInit()).WillRepeatedly(Return(true));

  //===--------------------------------------------------------------------===//
  // Setup right table
  //===--------------------------------------------------------------------===//
  if (join_test_type == BASIC_TEST || join_test_type == COMPLICATED_TEST ||
      join_test_type == SPEED_TEST) {
    ExpectNormalTileResults(right_table_tile_group_count,
                            &right_table_scan_executor,
                            right_table_logical_tile_ptrs);
  } else if (join_test_type == BOTH_TABLES_EMPTY) {
    ExpectEmptyTileResult(&right_table_scan_executor);
  } else if (join_test_type == LEFT_TABLE_EMPTY) {
    if (join_type == JOIN_TYPE_INNER || join_type == JOIN_TYPE_LEFT) {
      // For hash join, we always build the hash table from right child
      if (join_algorithm == PLAN_NODE_TYPE_HASHJOIN) {
        ExpectNormalTileResults(right_table_tile_group_count,
                                &right_table_scan_executor,
                                right_table_logical_tile_ptrs);
      } else {
        ExpectMoreThanOneTileResults(&right_table_scan_executor,
                                     right_table_logical_tile_ptrs);
      }

    } else if (join_type == JOIN_TYPE_OUTER || join_type == JOIN_TYPE_RIGHT) {
      ExpectNormalTileResults(right_table_tile_group_count,
                              &right_table_scan_executor,
                              right_table_logical_tile_ptrs);
    }
  } else if (join_test_type == RIGHT_TABLE_EMPTY) {
    ExpectEmptyTileResult(&right_table_scan_executor);
  }

  //===--------------------------------------------------------------------===//
  // Setup join plan nodes and executors and run them
  //===--------------------------------------------------------------------===//

  oid_t result_tuple_count = 0;
  oid_t tuples_with_null = 0;
  auto projection = JoinTestsUtil::CreateProjection();
  // setup the projection schema
  auto schema = CreateJoinSchema();

  // Construct predicate
  std::unique_ptr<const expression::AbstractExpression> predicate(
      JoinTestsUtil::CreateJoinPredicate());

  // Differ based on join algorithm
  switch (join_algorithm) {
    case PLAN_NODE_TYPE_NESTLOOP: {
      // Create nested loop join plan node.
      planner::NestedLoopJoinPlan nested_loop_join_node(
          join_type, std::move(predicate), std::move(projection), schema);

      // Run the nested loop join executor
      executor::NestedLoopJoinExecutor nested_loop_join_executor(
          &nested_loop_join_node, nullptr);

      // Construct the executor tree
      nested_loop_join_executor.AddChild(&left_table_scan_executor);
      nested_loop_join_executor.AddChild(&right_table_scan_executor);

      // Run the nested loop join executor
      EXPECT_TRUE(nested_loop_join_executor.Init());
      while (nested_loop_join_executor.Execute() == true) {
        std::unique_ptr<executor::LogicalTile> result_logical_tile(
            nested_loop_join_executor.GetOutput());

        if (result_logical_tile != nullptr) {
          result_tuple_count += result_logical_tile->GetTupleCount();
          tuples_with_null +=
              CountTuplesWithNullFields(result_logical_tile.get());
          ValidateJoinLogicalTile(result_logical_tile.get());
          LOG_TRACE("result tile info: %s",
                    result_logical_tile->GetInfo().c_str());
        }
      }

    } break;

    case PLAN_NODE_TYPE_MERGEJOIN: {
      // Create join clauses
      std::vector<planner::MergeJoinPlan::JoinClause> join_clauses;
      join_clauses = CreateJoinClauses();

      // Create merge join plan node
      planner::MergeJoinPlan merge_join_node(join_type, std::move(predicate),
                                             std::move(projection), schema,
                                             join_clauses);

      // Construct the merge join executor
      executor::MergeJoinExecutor merge_join_executor(&merge_join_node,
                                                      nullptr);

      // Construct the executor tree
      merge_join_executor.AddChild(&left_table_scan_executor);
      merge_join_executor.AddChild(&right_table_scan_executor);

      // Run the merge join executor
      EXPECT_TRUE(merge_join_executor.Init());
      while (merge_join_executor.Execute() == true) {
        std::unique_ptr<executor::LogicalTile> result_logical_tile(
            merge_join_executor.GetOutput());

        if (result_logical_tile != nullptr) {
          result_tuple_count += result_logical_tile->GetTupleCount();
          tuples_with_null +=
              CountTuplesWithNullFields(result_logical_tile.get());
          ValidateJoinLogicalTile(result_logical_tile.get());
          LOG_TRACE("%s", result_logical_tile->GetInfo().c_str());
        }
      }

    } break;

    case PLAN_NODE_TYPE_HASHJOIN: {
      // Create hash plan node
      expression::AbstractExpression *right_table_attr_1 =
          new expression::TupleValueExpression(common::Type::INTEGER, 1, 1);

      std::vector<std::unique_ptr<const expression::AbstractExpression>>
          hash_keys;
      hash_keys.emplace_back(right_table_attr_1);

      std::vector<std::unique_ptr<const expression::AbstractExpression>>
          left_hash_keys;
      left_hash_keys.emplace_back(
          std::unique_ptr<expression::AbstractExpression>{
              new expression::TupleValueExpression(common::Type::INTEGER, 0,
                                                   1)});

      std::vector<std::unique_ptr<const expression::AbstractExpression>>
          right_hash_keys;
      right_hash_keys.emplace_back(
          std::unique_ptr<expression::AbstractExpression>{
              new expression::TupleValueExpression(common::Type::INTEGER, 1,
                                                   1)});

      // Create hash plan node
      planner::HashPlan hash_plan_node(hash_keys);

      // Construct the hash executor
      executor::HashExecutor hash_executor(&hash_plan_node, nullptr);

      // Create hash join plan node.
      planner::HashJoinPlan hash_join_plan_node(join_type, std::move(predicate),
                                                std::move(projection), schema);

      // Construct the hash join executor
      executor::HashJoinExecutor hash_join_executor(&hash_join_plan_node,
                                                    nullptr);

      // Construct the executor tree
      hash_join_executor.AddChild(&left_table_scan_executor);
      hash_join_executor.AddChild(&hash_executor);

      hash_executor.AddChild(&right_table_scan_executor);

      // Run the hash_join_executor
      EXPECT_TRUE(hash_join_executor.Init());
      while (hash_join_executor.Execute() == true) {
        std::unique_ptr<executor::LogicalTile> result_logical_tile(
            hash_join_executor.GetOutput());

        if (result_logical_tile != nullptr) {
          result_tuple_count += result_logical_tile->GetTupleCount();
          tuples_with_null +=
              CountTuplesWithNullFields(result_logical_tile.get());
          ValidateJoinLogicalTile(result_logical_tile.get());
          LOG_TRACE("%s", result_logical_tile->GetInfo().c_str());
        }
      }

    } break;

    default:
      throw Exception("Unsupported join algorithm : " +
                      std::to_string(join_algorithm));
      break;
  }

  //===--------------------------------------------------------------------===//
  // Execute test
  //===--------------------------------------------------------------------===//

  if (join_test_type == BASIC_TEST) {
    // Check output
    switch (join_type) {
      case JOIN_TYPE_INNER:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_LEFT:
        EXPECT_EQ(result_tuple_count, 15);
        EXPECT_EQ(tuples_with_null, 5);
        break;

      case JOIN_TYPE_RIGHT:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_OUTER:
        EXPECT_EQ(result_tuple_count, 15);
        EXPECT_EQ(tuples_with_null, 5);
        break;

      default:
        throw Exception("Unsupported join type : " + std::to_string(join_type));
        break;
    }

  } else if (join_test_type == BOTH_TABLES_EMPTY) {
    // Check output
    switch (join_type) {
      case JOIN_TYPE_INNER:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_LEFT:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_RIGHT:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_OUTER:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      default:
        throw Exception("Unsupported join type : " + std::to_string(join_type));
        break;
    }

  } else if (join_test_type == COMPLICATED_TEST) {
    // Check output
    switch (join_type) {
      case JOIN_TYPE_INNER:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_LEFT:
        EXPECT_EQ(result_tuple_count, 17);
        EXPECT_EQ(tuples_with_null, 7);
        break;

      case JOIN_TYPE_RIGHT:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_OUTER:
        EXPECT_EQ(result_tuple_count, 17);
        EXPECT_EQ(tuples_with_null, 7);
        break;

      default:
        throw Exception("Unsupported join type : " + std::to_string(join_type));
        break;
    }

  } else if (join_test_type == LEFT_TABLE_EMPTY) {
    // Check output
    switch (join_type) {
      case JOIN_TYPE_INNER:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_LEFT:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_RIGHT:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 10);
        break;

      case JOIN_TYPE_OUTER:
        EXPECT_EQ(result_tuple_count, 10);
        EXPECT_EQ(tuples_with_null, 10);
        break;

      default:
        throw Exception("Unsupported join type : " + std::to_string(join_type));
        break;
    }
  } else if (join_test_type == RIGHT_TABLE_EMPTY) {
    // Check output
    switch (join_type) {
      case JOIN_TYPE_INNER:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_LEFT:
        EXPECT_EQ(result_tuple_count, 15);
        EXPECT_EQ(tuples_with_null, 15);
        break;

      case JOIN_TYPE_RIGHT:
        EXPECT_EQ(result_tuple_count, 0);
        EXPECT_EQ(tuples_with_null, 0);
        break;

      case JOIN_TYPE_OUTER:
        EXPECT_EQ(result_tuple_count, 15);
        EXPECT_EQ(tuples_with_null, 15);
        break;

      default:
        throw Exception("Unsupported join type : " + std::to_string(join_type));
        break;
    }
  }
}

oid_t CountTuplesWithNullFields(executor::LogicalTile *logical_tile) {
  PL_ASSERT(logical_tile);

  // Get column count
  auto column_count = logical_tile->GetColumnCount();
  oid_t tuples_with_null = 0;

  // Go over the tile
  for (auto logical_tile_itr : *logical_tile) {
    const expression::ContainerTuple<executor::LogicalTile> join_tuple(
        logical_tile, logical_tile_itr);

    // Go over all the fields and check for null values
    for (oid_t col_itr = 0; col_itr < column_count; col_itr++) {
      common::Value val = (join_tuple.GetValue(col_itr));
      if (val.IsNull()) {
        tuples_with_null++;
        break;
      }
    }
  }

  return tuples_with_null;
}

void ValidateJoinLogicalTile(executor::LogicalTile *logical_tile) {
  PL_ASSERT(logical_tile);

  // Get column count
  auto column_count = logical_tile->GetColumnCount();

  // Check # of columns
  EXPECT_EQ(column_count, 4);

  // Check the attribute values
  // Go over the tile
  for (auto logical_tile_itr : *logical_tile) {
    const expression::ContainerTuple<executor::LogicalTile> join_tuple(
        logical_tile, logical_tile_itr);

    // Check the join fields
    common::Value left_tuple_join_attribute_val = (join_tuple.GetValue(0));
    common::Value right_tuple_join_attribute_val = (join_tuple.GetValue(1));
    common::Value cmp = (left_tuple_join_attribute_val.CompareEquals(
        right_tuple_join_attribute_val));
    EXPECT_TRUE(cmp.IsNull() || cmp.IsTrue());
  }
}

void ValidateNestedLoopJoinLogicalTile(executor::LogicalTile *logical_tile) {
  PL_ASSERT(logical_tile);

  // Get column count
  auto column_count = logical_tile->GetColumnCount();

  // Check # of columns
  EXPECT_EQ(column_count, 4);

  // Check the attribute values
  // Go over the tile
  for (auto logical_tile_itr : *logical_tile) {
    const expression::ContainerTuple<executor::LogicalTile> join_tuple(
        logical_tile, logical_tile_itr);

    // Check the join fields
    common::Value left_tuple_join_attribute_val = (join_tuple.GetValue(2));
    common::Value right_tuple_join_attribute_val = (join_tuple.GetValue(3));
    common::Value cmp = (left_tuple_join_attribute_val.CompareEquals(
        right_tuple_join_attribute_val));
    EXPECT_TRUE(cmp.IsNull() || cmp.IsTrue());
  }
}

void ExpectEmptyTileResult(MockExecutor *table_scan_executor) {
  // Expect zero result tiles from the child
  EXPECT_CALL(*table_scan_executor, DExecute()).WillOnce(Return(false));
}

void ExpectMoreThanOneTileResults(
    MockExecutor *table_scan_executor,
    std::vector<std::unique_ptr<executor::LogicalTile>> &
        table_logical_tile_ptrs) {
  // Expect more than one result tiles from the child, but only get one of them
  EXPECT_CALL(*table_scan_executor, DExecute()).WillOnce(Return(true));
  EXPECT_CALL(*table_scan_executor, GetOutput())
      .WillOnce(Return(table_logical_tile_ptrs[0].release()));
}

void ExpectNormalTileResults(
    size_t table_tile_group_count, MockExecutor *table_scan_executor,
    std::vector<std::unique_ptr<executor::LogicalTile>> &
        table_logical_tile_ptrs) {
  // Return true for the first table_tile_group_count times
  // Then return false after that
  {
    testing::Sequence execute_sequence;
    for (size_t table_tile_group_itr = 0;
         table_tile_group_itr < table_tile_group_count + 1;
         table_tile_group_itr++) {
      // Return true for the first table_tile_group_count times
      if (table_tile_group_itr < table_tile_group_count) {
        EXPECT_CALL(*table_scan_executor, DExecute())
            .InSequence(execute_sequence)
            .WillOnce(Return(true));
      } else  // Return false after that
      {
        EXPECT_CALL(*table_scan_executor, DExecute())
            .InSequence(execute_sequence)
            .WillOnce(Return(false));
      }
    }
  }
  // Return the appropriate logical tiles for the first table_tile_group_count
  // times
  {
    testing::Sequence get_output_sequence;
    for (size_t table_tile_group_itr = 0;
         table_tile_group_itr < table_tile_group_count;
         table_tile_group_itr++) {
      EXPECT_CALL(*table_scan_executor, GetOutput())
          .InSequence(get_output_sequence)
          .WillOnce(
               Return(table_logical_tile_ptrs[table_tile_group_itr].release()));
    }
  }
}
}  // namespace test
}  // namespace peloton
