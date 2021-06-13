/* Copyright 2020 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "ml_metadata/tools/mlmd_bench/read_nodes_by_properties_workload.h"

#include <gtest/gtest.h>
#include "absl/memory/memory.h"
#include "ml_metadata/metadata_store/metadata_store.h"
#include "ml_metadata/metadata_store/metadata_store_factory.h"
#include "ml_metadata/metadata_store/test_util.h"
#include "ml_metadata/metadata_store/types.h"
#include "ml_metadata/proto/metadata_store.pb.h"
#include "ml_metadata/proto/metadata_store_service.pb.h"
#include "ml_metadata/tools/mlmd_bench/proto/mlmd_bench.pb.h"
#include "ml_metadata/tools/mlmd_bench/util.h"
#include "tensorflow/core/lib/core/status_test_util.h"

namespace ml_metadata {
namespace {

constexpr int kNumberOfOperations = 100;
constexpr int kNumberOfExistedTypesInDb = 100;
constexpr int kNumberOfExistedNodesInDb = 300;

// Enumerates the workload configurations as the test parameters that ensure
// test coverage.
std::vector<WorkloadConfig> EnumerateConfigs() {
  std::vector<WorkloadConfig> configs;
  std::vector<ReadNodesByPropertiesConfig::Specification> specifications = {
      ReadNodesByPropertiesConfig::ARTIFACTS_BY_ID,
      ReadNodesByPropertiesConfig::EXECUTIONS_BY_ID,
      ReadNodesByPropertiesConfig::CONTEXTS_BY_ID,
      ReadNodesByPropertiesConfig::ARTIFACTS_BY_TYPE,
      ReadNodesByPropertiesConfig::EXECUTIONS_BY_TYPE,
      ReadNodesByPropertiesConfig::CONTEXTS_BY_TYPE,
      ReadNodesByPropertiesConfig::ARTIFACT_BY_TYPE_AND_NAME,
      ReadNodesByPropertiesConfig::EXECUTION_BY_TYPE_AND_NAME,
      ReadNodesByPropertiesConfig::CONTEXT_BY_TYPE_AND_NAME,
      ReadNodesByPropertiesConfig::ARTIFACTS_BY_URI};

  for (const ReadNodesByPropertiesConfig::Specification& specification :
       specifications) {
    WorkloadConfig config;
    config.set_num_operations(kNumberOfOperations);
    config.mutable_read_nodes_by_properties_config()->set_specification(
        specification);
    if (specification == ReadNodesByPropertiesConfig::ARTIFACTS_BY_ID ||
        specification == ReadNodesByPropertiesConfig::EXECUTIONS_BY_ID ||
        specification == ReadNodesByPropertiesConfig::CONTEXTS_BY_ID ||
        specification == ReadNodesByPropertiesConfig::ARTIFACTS_BY_URI) {
      config.mutable_read_nodes_by_properties_config()
          ->mutable_num_of_parameters()
          ->set_minimum(1);
      config.mutable_read_nodes_by_properties_config()
          ->mutable_num_of_parameters()
          ->set_maximum(10);
    }
    configs.push_back(config);
  }

  return configs;
}

// Test fixture that uses the same data configuration for multiple following
// parameterized ReadNodesByProperties tests.
// The parameter here is the specific Workload configuration that contains
// the ReadNodesByProperties configuration and the number of operations.
class ReadNodesByPropertiesParameterizedTestFixture
    : public ::testing::TestWithParam<WorkloadConfig> {
 protected:
  void SetUp() override {
    ConnectionConfig mlmd_config;
    // Uses a fake in-memory SQLite database for testing.
    mlmd_config.mutable_fake_database();
    TF_ASSERT_OK(CreateMetadataStore(mlmd_config, &store_));
    read_nodes_by_properties_ = absl::make_unique<ReadNodesByProperties>(
        ReadNodesByProperties(GetParam().read_nodes_by_properties_config(),
                              GetParam().num_operations()));
    TF_ASSERT_OK(InsertTypesInDb(
        /*num_artifact_types=*/kNumberOfExistedTypesInDb,
        /*num_execution_types=*/kNumberOfExistedTypesInDb,
        /*num_context_types=*/kNumberOfExistedTypesInDb, *store_));

    TF_ASSERT_OK(InsertNodesInDb(
        /*num_artifact_nodes=*/kNumberOfExistedNodesInDb,
        /*num_execution_nodes=*/kNumberOfExistedNodesInDb,
        /*num_context_nodes=*/kNumberOfExistedNodesInDb, *store_));
  }

  std::unique_ptr<ReadNodesByProperties> read_nodes_by_properties_;
  std::unique_ptr<MetadataStore> store_;
};

// Tests the SetUpImpl() for ReadNodesByProperties. Checks the SetUpImpl()
// indeed prepares a list of work items whose length is the same as the
// specified number of operations.
TEST_P(ReadNodesByPropertiesParameterizedTestFixture, SetUpImplTest) {
  TF_ASSERT_OK(read_nodes_by_properties_->SetUp(store_.get()));
  EXPECT_EQ(GetParam().num_operations(),
            read_nodes_by_properties_->num_operations());
}

// Tests the RunOpImpl() for ReadNodesByProperties. Checks indeed all the work
// items have been executed and some bytes are transferred during the reading
// process.
TEST_P(ReadNodesByPropertiesParameterizedTestFixture, RunOpImplTest) {
  TF_ASSERT_OK(read_nodes_by_properties_->SetUp(store_.get()));

  int64 total_done = 0;
  ThreadStats stats;
  stats.Start();
  for (int64 i = 0; i < read_nodes_by_properties_->num_operations(); ++i) {
    OpStats op_stats;
    TF_ASSERT_OK(read_nodes_by_properties_->RunOp(i, store_.get(), op_stats));
    stats.Update(op_stats, total_done);
  }
  stats.Stop();
  EXPECT_EQ(stats.done(), GetParam().num_operations());
  // Checks that the transferred bytes is greater that 0(the reading process
  // indeed occurred).
  EXPECT_GT(stats.bytes(), 0);
}

INSTANTIATE_TEST_CASE_P(ReadNodesByPropertiesTest,
                        ReadNodesByPropertiesParameterizedTestFixture,
                        ::testing::ValuesIn(EnumerateConfigs()));

}  // namespace
}  // namespace ml_metadata
