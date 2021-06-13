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
#include "ml_metadata/tools/mlmd_bench/thread_runner.h"

#include <gtest/gtest.h>
#include "ml_metadata/metadata_store/metadata_store.h"
#include "ml_metadata/metadata_store/metadata_store_factory.h"
#include "ml_metadata/metadata_store/test_util.h"
#include "ml_metadata/proto/metadata_store_service.pb.h"
#include "ml_metadata/tools/mlmd_bench/benchmark.h"
#include "ml_metadata/tools/mlmd_bench/proto/mlmd_bench.pb.h"
#include "tensorflow/core/lib/core/status_test_util.h"

namespace ml_metadata {
namespace {

void TestThreadRunner(const int num_thread) {
  MLMDBenchConfig mlmd_bench_config;
  mlmd_bench_config.mutable_thread_env_config()->set_num_threads(num_thread);
  mlmd_bench_config.add_workload_configs()->CopyFrom(
      testing::ParseTextProtoOrDie<WorkloadConfig>(R"(
        fill_types_config: {
          update: false
          specification: ARTIFACT_TYPE
          num_properties: { minimum: 1 maximum: 10 }
        }
        num_operations: 100
      )"));
  // Uses a fake in-memory SQLite database for testing.
  const std::string filename_uri =
      absl::StrCat(::testing::TempDir(), "mlmd-bench-test_", num_thread, ".db");
  mlmd_bench_config.mutable_mlmd_config()->mutable_sqlite()->set_filename_uri(
      filename_uri);
  Benchmark benchmark(mlmd_bench_config);
  ThreadRunner runner(mlmd_bench_config.mlmd_config(),
                      mlmd_bench_config.thread_env_config().num_threads());
  TF_ASSERT_OK(runner.Run(benchmark));

  std::unique_ptr<MetadataStore> store;
  TF_ASSERT_OK(CreateMetadataStore(mlmd_bench_config.mlmd_config(), &store));

  GetArtifactTypesResponse get_response;
  TF_ASSERT_OK(store->GetArtifactTypes(/*request=*/{}, &get_response));
  // Checks that the workload indeed be executed by the thread_runner.
  EXPECT_EQ(get_response.artifact_types_size(),
            mlmd_bench_config.workload_configs()[0].num_operations());

  // Checks for valid performance report.
  ASSERT_THAT(benchmark.mlmd_bench_report().summaries(), ::testing::SizeIs(1));
  WorkloadConfigResult summary = benchmark.mlmd_bench_report().summaries()[0];
  EXPECT_GT(summary.microseconds_per_operation(), 0);
  EXPECT_GT(summary.bytes_per_second(), 0);
}

// Tests the Run() of ThreadRunner class in single-thread mode.
TEST(ThreadRunnerTest, RunInSingleThreadTest) { TestThreadRunner(1); }

// Tests the Run() of ThreadRunner class in multi-thread mode.
TEST(ThreadRunnerTest, RunInMultiThreadTest) { TestThreadRunner(10); }

}  // namespace
}  // namespace ml_metadata
