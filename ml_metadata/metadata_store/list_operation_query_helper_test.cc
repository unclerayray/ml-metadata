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
#include "ml_metadata/metadata_store/list_operation_query_helper.h"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/strings/escaping.h"
#include "ml_metadata/metadata_store/test_util.h"
#include "ml_metadata/proto/metadata_store.pb.h"

namespace ml_metadata {
namespace {

ListOperationOptions BasicListOperationOptionsDesc() {
  return testing::ParseTextProtoOrDie<ListOperationOptions>(R"pb(
    max_result_size: 1,
    order_by_field: { field: CREATE_TIME, is_asc: false }
  )pb");
}

ListOperationOptions BasicListOperationOptionsAsc() {
  return testing::ParseTextProtoOrDie<ListOperationOptions>(R"pb(
    max_result_size: 1,
    order_by_field: { field: CREATE_TIME, is_asc: true }
  )pb");
}

ListOperationNextPageToken BasicListOperationNextPageToken() {
  return testing::ParseTextProtoOrDie<ListOperationNextPageToken>(R"pb(
    field_offset: 56894,
    id_offset: 100
  )pb");
}

TEST(ListOperationQueryHelperTest, OrderingWhereClauseDesc) {
  ListOperationOptions options = BasicListOperationOptionsDesc();
  ListOperationNextPageToken next_page_token =
      BasicListOperationNextPageToken();
  *next_page_token.mutable_set_options() = options;
  options.set_next_page_token(
      absl::WebSafeBase64Escape(next_page_token.SerializeAsString()));
  std::string where_clause;
  ASSERT_EQ(absl::OkStatus(),
            AppendOrderingThresholdClause(options, where_clause));
  EXPECT_EQ(where_clause,
            " `create_time_since_epoch` <= 56894 AND `id` < 100 ");
}

TEST(ListOperationQueryHelperTest, OrderingWhereClauseAsc) {
  ListOperationOptions options = BasicListOperationOptionsAsc();
  ListOperationNextPageToken next_page_token =
      BasicListOperationNextPageToken();
  *next_page_token.mutable_set_options() = options;
  options.set_next_page_token(
      absl::WebSafeBase64Escape(next_page_token.SerializeAsString()));

  std::string where_clause;
  ASSERT_EQ(absl::OkStatus(),
            AppendOrderingThresholdClause(options, where_clause));
  EXPECT_EQ(where_clause,
            " `create_time_since_epoch` >= 56894 AND `id` > 100 ");
}

TEST(ListOperationQueryHelperTest, OrderingOnLastUpdateTimeDesc) {
  ListOperationOptions options =
      testing::ParseTextProtoOrDie<ListOperationOptions>(R"pb(
        max_result_size: 1,
        order_by_field: { field: LAST_UPDATE_TIME, is_asc: false }
      )pb");

  ListOperationNextPageToken next_page_token;
  next_page_token.add_listed_ids(6);
  next_page_token.add_listed_ids(5);
  next_page_token.set_field_offset(56894);
  *next_page_token.mutable_set_options() = options;
  options.set_next_page_token(
      absl::WebSafeBase64Escape(next_page_token.SerializeAsString()));
  std::string where_clause;
  ASSERT_EQ(absl::OkStatus(),
            AppendOrderingThresholdClause(options, where_clause));
  EXPECT_EQ(where_clause,
            " `last_update_time_since_epoch` <= 56894 AND `id` NOT IN (6,5) ");
}

TEST(ListOperationQueryHelperTest, OrderingWhereClauseById) {
  ListOperationOptions options =
      testing::ParseTextProtoOrDie<ListOperationOptions>(R"pb(
        max_result_size: 1,
        order_by_field: { field: ID, is_asc: false }
      )pb");

  ListOperationNextPageToken next_page_token;
  next_page_token.set_field_offset(100);
  *next_page_token.mutable_set_options() = options;
  options.set_next_page_token(
      absl::WebSafeBase64Escape(next_page_token.SerializeAsString()));
  std::string where_clause;
  ASSERT_EQ(absl::OkStatus(),
            AppendOrderingThresholdClause(options, where_clause));
  EXPECT_EQ(where_clause, " `id` < 100 ");
}

TEST(ListOperationQueryHelperTest, OrderByClauseDesc) {
  const ListOperationOptions options = BasicListOperationOptionsDesc();
  std::string order_by_clause;
  ASSERT_EQ(absl::OkStatus(), AppendOrderByClause(options, order_by_clause));
  EXPECT_EQ(order_by_clause,
            " ORDER BY `create_time_since_epoch` DESC, `id` DESC ");
}

TEST(ListOperationQueryHelperTest, OrderByClauseAsc) {
  const ListOperationOptions options = BasicListOperationOptionsAsc();
  std::string order_by_clause;
  ASSERT_EQ(absl::OkStatus(), AppendOrderByClause(options, order_by_clause));
  EXPECT_EQ(order_by_clause,
            " ORDER BY `create_time_since_epoch` ASC, `id` ASC ");
}

TEST(ListOperationQueryHelperTest, OrderByClauseById) {
  const ListOperationOptions options =
      testing::ParseTextProtoOrDie<ListOperationOptions>(R"pb(
        max_result_size: 1,
        order_by_field: { field: ID, is_asc: false }
      )pb");
  std::string order_by_clause;
  ASSERT_EQ(absl::OkStatus(), AppendOrderByClause(options, order_by_clause));
  EXPECT_EQ(order_by_clause, " ORDER BY `id` DESC ");
}

TEST(ListOperationQueryHelperTest, LimitClause) {
  const ListOperationOptions options = BasicListOperationOptionsDesc();
  std::string limit_clause;
  ASSERT_EQ(absl::OkStatus(), AppendLimitClause(options, limit_clause));
  EXPECT_EQ(limit_clause, " LIMIT 1 ");
}

TEST(ListOperationQueryHelperTest, LimitOverMaxClause) {
  ListOperationOptions options = BasicListOperationOptionsDesc();
  options.set_max_result_size(200);
  std::string limit_clause;
  ASSERT_EQ(absl::OkStatus(), AppendLimitClause(options, limit_clause));
  EXPECT_EQ(limit_clause, " LIMIT 101 ");
}

TEST(ListOperationQueryHelperTest, InvalidLimit) {
  ListOperationOptions options = BasicListOperationOptionsDesc();
  options.set_max_result_size(0);
  std::string limit_clause;
  EXPECT_TRUE(
      absl::IsInvalidArgument(AppendLimitClause(options, limit_clause)));
}
}  // namespace
}  // namespace ml_metadata
