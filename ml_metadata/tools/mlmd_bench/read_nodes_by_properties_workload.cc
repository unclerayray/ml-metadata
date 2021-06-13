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

#include <random>
#include <vector>

#include "absl/time/clock.h"
#include "ml_metadata/metadata_store/metadata_store.h"
#include "ml_metadata/metadata_store/types.h"
#include "ml_metadata/proto/metadata_store.pb.h"
#include "ml_metadata/proto/metadata_store_service.pb.h"
#include "ml_metadata/tools/mlmd_bench/proto/mlmd_bench.pb.h"
#include "ml_metadata/tools/mlmd_bench/util.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/lib/core/status.h"

namespace ml_metadata {
namespace {

constexpr int64 kInt64IdSize = 8;
constexpr int64 kInt64TypeIdSize = 8;
constexpr int64 kInt64CreateTimeSize = 8;
constexpr int64 kInt64LastUpdateTimeSize = 8;
constexpr int64 kEnumStateSize = 1;

// Gets all nodes inside db. Returns detailed error if query executions failed.
// Returns FAILED_PRECONDITION if there is no nodes inside db to read from.
tensorflow::Status GetAndValidateExistingNodes(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    MetadataStore& store, std::vector<Node>& existing_nodes) {
  TF_RETURN_IF_ERROR(
      GetExistingNodes(read_nodes_by_properties_config, store, existing_nodes));
  if (existing_nodes.empty()) {
    return tensorflow::errors::FailedPrecondition(
        "There are no nodes inside db to read from!");
  }
  return tensorflow::Status::OK();
}

// Gets the transferred bytes for certain `properties` and returns their bytes.
int64 GetTransferredBytesForNodeProperties(
    const google::protobuf::Map<std::string, Value>& properties) {
  int64 bytes = 0;
  for (auto& pair : properties) {
    // Includes the bytes for properties' name size.
    bytes += pair.first.size();
    // Includes the bytes for properties' value size.
    bytes += pair.second.string_value().size();
  }
  return bytes;
}

// TODO(b/152220177) Moves the GetTransferredBytes to util with better
// docstrings and tests.
// Gets the transferred bytes for certain Artifact.
int64 GetTransferredBytes(const Artifact& node) {
  int64 bytes = kInt64IdSize + kInt64TypeIdSize + kInt64CreateTimeSize +
                kInt64LastUpdateTimeSize;
  bytes += node.name().size();
  bytes += node.type().size();
  bytes += node.uri().size();
  bytes += kEnumStateSize;
  bytes += GetTransferredBytesForNodeProperties(node.properties());
  bytes += GetTransferredBytesForNodeProperties(node.custom_properties());
  return bytes;
}

// Gets the transferred bytes for certain Execution.
int64 GetTransferredBytes(const Execution& node) {
  int64 bytes = kInt64IdSize + kInt64TypeIdSize + kInt64CreateTimeSize +
                kInt64LastUpdateTimeSize;
  bytes += node.name().size();
  bytes += node.type().size();
  bytes += kEnumStateSize;
  bytes += GetTransferredBytesForNodeProperties(node.properties());
  bytes += GetTransferredBytesForNodeProperties(node.custom_properties());
  return bytes;
}

// Gets the transferred bytes for certain Context.
int64 GetTransferredBytes(const Context& node) {
  int64 bytes = kInt64IdSize + kInt64TypeIdSize + kInt64CreateTimeSize +
                kInt64LastUpdateTimeSize;
  bytes += node.name().size();
  bytes += node.type().size();
  bytes += GetTransferredBytesForNodeProperties(node.properties());
  bytes += GetTransferredBytesForNodeProperties(node.custom_properties());
  return bytes;
}

// Gets the transferred bytes for all nodes under a certain type.
template <typename NT>
int64 GetTransferredBytesForAllNodesUnderAType(
    const std::string type_name, const std::vector<Node>& existing_nodes) {
  int64 bytes = 0;
  for (auto& node : existing_nodes) {
    if (absl::get<NT>(node).type() == type_name) {
      bytes += GetTransferredBytes(absl::get<NT>(node));
    }
  }
  return bytes;
}

// SetUpImpl() for the specifications to read nodes by a list of ids in db.
// Returns detailed error if query executions failed.
tensorflow::Status SetUpImplForReadNodesByIds(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    const std::vector<Node>& existing_nodes,
    std::uniform_int_distribution<int64>& node_index_dist,
    std::minstd_rand0& gen, ReadNodesByPropertiesWorkItemType& request,
    int64& curr_bytes) {
  UniformDistribution num_ids_proto_dist =
      read_nodes_by_properties_config.num_of_parameters();
  std::uniform_int_distribution<int64> num_ids_dist{
      num_ids_proto_dist.minimum(), num_ids_proto_dist.maximum()};
  // Specifies the number of ids to put inside each request.
  const int64 num_ids = num_ids_dist(gen);
  for (int64 i = 0; i < num_ids; ++i) {
    // Selects from existing nodes uniformly to get a node id.
    const int64 node_index = node_index_dist(gen);
    switch (read_nodes_by_properties_config.specification()) {
      case ReadNodesByPropertiesConfig::ARTIFACTS_BY_ID: {
        request = GetArtifactsByIDRequest();
        absl::get<GetArtifactsByIDRequest>(request).add_artifact_ids(
            absl::get<Artifact>(existing_nodes[node_index]).id());
        curr_bytes += GetTransferredBytes(
            absl::get<Artifact>(existing_nodes[node_index]));
        break;
      }
      case ReadNodesByPropertiesConfig::EXECUTIONS_BY_ID: {
        request = GetExecutionsByIDRequest();
        absl::get<GetExecutionsByIDRequest>(request).add_execution_ids(
            absl::get<Execution>(existing_nodes[node_index]).id());
        curr_bytes += GetTransferredBytes(
            absl::get<Execution>(existing_nodes[node_index]));
        break;
      }
      case ReadNodesByPropertiesConfig::CONTEXTS_BY_ID: {
        request = GetContextsByIDRequest();
        absl::get<GetContextsByIDRequest>(request).add_context_ids(
            absl::get<Context>(existing_nodes[node_index]).id());
        curr_bytes +=
            GetTransferredBytes(absl::get<Context>(existing_nodes[node_index]));
        break;
      }
      default:
        LOG(FATAL) << "Wrong ReadNodesByProperties specification for read "
                      "nodes by ids in db.";
    }
  }
  return tensorflow::Status::OK();
}

// SetUpImpl() for the specifications to read artifacts by a list of uris in db.
// Returns detailed error if query executions failed.
tensorflow::Status SetUpImplForReadArtifactsByURIs(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    const std::vector<Node>& existing_nodes,
    std::uniform_int_distribution<int64>& node_index_dist,
    std::minstd_rand0& gen, ReadNodesByPropertiesWorkItemType& request,
    int64& curr_bytes) {
  if (read_nodes_by_properties_config.specification() !=
      ReadNodesByPropertiesConfig::ARTIFACTS_BY_URI) {
    LOG(FATAL) << "Wrong ReadNodesByProperties specification for read "
                  "artifacts by uris in db.";
  }
  UniformDistribution num_uris_proto_dist =
      read_nodes_by_properties_config.num_of_parameters();
  std::uniform_int_distribution<int64> num_uris_dist{
      num_uris_proto_dist.minimum(), num_uris_proto_dist.maximum()};
  // Specifies the number of uris to put inside each request.
  const int64 num_uris = num_uris_dist(gen);
  for (int64 i = 0; i < num_uris; ++i) {
    // Selects from existing nodes uniformly to get a node uri.
    const int64 node_index = node_index_dist(gen);
    request = GetArtifactsByURIRequest();
    absl::get<GetArtifactsByURIRequest>(request).add_uris(
        absl::get<Artifact>(existing_nodes[node_index]).uri());
    curr_bytes +=
        GetTransferredBytes(absl::get<Artifact>(existing_nodes[node_index]));
  }
  return tensorflow::Status::OK();
}

// SetUpImpl() for the specifications to read artifacts by type in db.
// Returns detailed error if query executions failed.
tensorflow::Status SetUpImplForReadNodesByType(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    const std::vector<Node>& existing_nodes,
    std::uniform_int_distribution<int64>& node_index_dist,
    std::minstd_rand0& gen, ReadNodesByPropertiesWorkItemType& request,
    int64& curr_bytes) {
  if (read_nodes_by_properties_config.has_num_of_parameters()) {
    LOG(FATAL) << "ReadNodesByType specification should not have a "
                  "`num_of_parameters` field!";
  }
  // Selects from existing nodes uniformly to get a type.
  const int64 node_index = node_index_dist(gen);
  switch (read_nodes_by_properties_config.specification()) {
    case ReadNodesByPropertiesConfig::ARTIFACTS_BY_TYPE: {
      request = GetArtifactsByTypeRequest();
      absl::get<GetArtifactsByTypeRequest>(request).set_type_name(
          absl::get<Artifact>(existing_nodes[node_index]).type());
      curr_bytes += GetTransferredBytesForAllNodesUnderAType<Artifact>(
          absl::get<Artifact>(existing_nodes[node_index]).type(),
          existing_nodes);
      break;
    }
    case ReadNodesByPropertiesConfig::EXECUTIONS_BY_TYPE: {
      request = GetExecutionsByTypeRequest();
      absl::get<GetExecutionsByTypeRequest>(request).set_type_name(
          absl::get<Execution>(existing_nodes[node_index]).type());
      curr_bytes += GetTransferredBytesForAllNodesUnderAType<Execution>(
          absl::get<Execution>(existing_nodes[node_index]).type(),
          existing_nodes);
      break;
    }
    case ReadNodesByPropertiesConfig::CONTEXTS_BY_TYPE: {
      request = GetContextsByTypeRequest();
      absl::get<GetContextsByTypeRequest>(request).set_type_name(
          absl::get<Context>(existing_nodes[node_index]).type());
      curr_bytes += GetTransferredBytesForAllNodesUnderAType<Context>(
          absl::get<Context>(existing_nodes[node_index]).type(),
          existing_nodes);
      break;
    }
    default:
      LOG(FATAL) << "Wrong ReadNodesByProperties specification for read nodes "
                    "by type in db.";
  }
  return tensorflow::Status::OK();
}

// SetUpImpl() for the specifications to read artifacts by name and type in db.
// Returns detailed error if query executions failed.
tensorflow::Status SetUpImplForReadNodeByTypeAndName(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    const std::vector<Node>& existing_nodes,
    std::uniform_int_distribution<int64>& node_index_dist,
    std::minstd_rand0& gen, ReadNodesByPropertiesWorkItemType& request,
    int64& curr_bytes) {
  if (read_nodes_by_properties_config.has_num_of_parameters()) {
    LOG(FATAL) << "ReadNodesByTypeAndName specification should not have a "
                  "`num_of_parameters` field!";
  }
  // Selects from existing nodes uniformly to get a name and a type.
  const int64 node_index = node_index_dist(gen);
  switch (read_nodes_by_properties_config.specification()) {
    case ReadNodesByPropertiesConfig::ARTIFACT_BY_TYPE_AND_NAME: {
      request = GetArtifactByTypeAndNameRequest();
      Artifact picked_node = absl::get<Artifact>(existing_nodes[node_index]);
      absl::get<GetArtifactByTypeAndNameRequest>(request).set_type_name(
          picked_node.type());
      absl::get<GetArtifactByTypeAndNameRequest>(request).set_artifact_name(
          picked_node.name());
      curr_bytes += GetTransferredBytes(picked_node);
      break;
    }
    case ReadNodesByPropertiesConfig::EXECUTION_BY_TYPE_AND_NAME: {
      request = GetExecutionByTypeAndNameRequest();
      Execution picked_node = absl::get<Execution>(existing_nodes[node_index]);
      absl::get<GetExecutionByTypeAndNameRequest>(request).set_type_name(
          picked_node.type());
      absl::get<GetExecutionByTypeAndNameRequest>(request).set_execution_name(
          picked_node.name());
      curr_bytes += GetTransferredBytes(picked_node);
      break;
    }
    case ReadNodesByPropertiesConfig::CONTEXT_BY_TYPE_AND_NAME: {
      request = GetContextByTypeAndNameRequest();
      Context picked_node = absl::get<Context>(existing_nodes[node_index]);
      absl::get<GetContextByTypeAndNameRequest>(request).set_type_name(
          picked_node.type());
      absl::get<GetContextByTypeAndNameRequest>(request).set_context_name(
          picked_node.name());
      curr_bytes += GetTransferredBytes(picked_node);
      break;
    }
    default:
      LOG(FATAL) << "Wrong ReadNodesByProperties specification for read node "
                    "by type and name in db.";
  }
  return tensorflow::Status::OK();
}

}  // namespace

ReadNodesByProperties::ReadNodesByProperties(
    const ReadNodesByPropertiesConfig& read_nodes_by_properties_config,
    const int64 num_operations)
    : read_nodes_by_properties_config_(read_nodes_by_properties_config),
      num_operations_(num_operations),
      name_(absl::StrCat(
          "READ_", read_nodes_by_properties_config_.Specification_Name(
                       read_nodes_by_properties_config_.specification()))) {}

tensorflow::Status ReadNodesByProperties::SetUpImpl(MetadataStore* store) {
  LOG(INFO) << "Setting up ...";

  // Gets all the specific nodes in db to choose from when reading nodes.
  // If there's no nodes in the store, returns FAILED_PRECONDITION error.
  std::vector<Node> existing_nodes;
  TF_RETURN_IF_ERROR(GetAndValidateExistingNodes(
      read_nodes_by_properties_config_, *store, existing_nodes));
  // Uniform distribution to select existing nodes uniformly.
  std::uniform_int_distribution<int64> node_index_dist{
      0, (int64)(existing_nodes.size() - 1)};
  std::minstd_rand0 gen(absl::ToUnixMillis(absl::Now()));

  for (int64 i = 0; i < num_operations_; ++i) {
    int64 curr_bytes = 0;
    ReadNodesByPropertiesWorkItemType read_request;
    switch (read_nodes_by_properties_config_.specification()) {
      case ReadNodesByPropertiesConfig::ARTIFACTS_BY_ID:
      case ReadNodesByPropertiesConfig::EXECUTIONS_BY_ID:
      case ReadNodesByPropertiesConfig::CONTEXTS_BY_ID:
        TF_RETURN_IF_ERROR(SetUpImplForReadNodesByIds(
            read_nodes_by_properties_config_, existing_nodes, node_index_dist,
            gen, read_request, curr_bytes));
        break;
      case ReadNodesByPropertiesConfig::ARTIFACTS_BY_URI:
        TF_RETURN_IF_ERROR(SetUpImplForReadArtifactsByURIs(
            read_nodes_by_properties_config_, existing_nodes, node_index_dist,
            gen, read_request, curr_bytes));
        break;
      case ReadNodesByPropertiesConfig::ARTIFACTS_BY_TYPE:
      case ReadNodesByPropertiesConfig::EXECUTIONS_BY_TYPE:
      case ReadNodesByPropertiesConfig::CONTEXTS_BY_TYPE:
        TF_RETURN_IF_ERROR(SetUpImplForReadNodesByType(
            read_nodes_by_properties_config_, existing_nodes, node_index_dist,
            gen, read_request, curr_bytes));
        break;
      case ReadNodesByPropertiesConfig::ARTIFACT_BY_TYPE_AND_NAME:
      case ReadNodesByPropertiesConfig::EXECUTION_BY_TYPE_AND_NAME:
      case ReadNodesByPropertiesConfig::CONTEXT_BY_TYPE_AND_NAME:
        TF_RETURN_IF_ERROR(SetUpImplForReadNodeByTypeAndName(
            read_nodes_by_properties_config_, existing_nodes, node_index_dist,
            gen, read_request, curr_bytes));
        break;
      default:
        LOG(FATAL) << "Wrong specification for ReadNodesByProperties!";
    }
    work_items_.emplace_back(read_request, curr_bytes);
  }
  return tensorflow::Status::OK();
}

// Executions of work items.
tensorflow::Status ReadNodesByProperties::RunOpImpl(
    const int64 work_items_index, MetadataStore* store) {
  switch (read_nodes_by_properties_config_.specification()) {
    case ReadNodesByPropertiesConfig::ARTIFACTS_BY_ID: {
      auto request = absl::get<GetArtifactsByIDRequest>(
          work_items_[work_items_index].first);
      GetArtifactsByIDResponse response;
      return store->GetArtifactsByID(request, &response);
    }
    case ReadNodesByPropertiesConfig::EXECUTIONS_BY_ID: {
      auto request = absl::get<GetExecutionsByIDRequest>(
          work_items_[work_items_index].first);
      GetExecutionsByIDResponse response;
      return store->GetExecutionsByID(request, &response);
    }
    case ReadNodesByPropertiesConfig::CONTEXTS_BY_ID: {
      auto request = absl::get<GetContextsByIDRequest>(
          work_items_[work_items_index].first);
      GetContextsByIDResponse response;
      return store->GetContextsByID(request, &response);
    }
    case ReadNodesByPropertiesConfig::ARTIFACTS_BY_TYPE: {
      auto request = absl::get<GetArtifactsByTypeRequest>(
          work_items_[work_items_index].first);
      GetArtifactsByTypeResponse response;
      return store->GetArtifactsByType(request, &response);
    }
    case ReadNodesByPropertiesConfig::EXECUTIONS_BY_TYPE: {
      auto request = absl::get<GetExecutionsByTypeRequest>(
          work_items_[work_items_index].first);
      GetExecutionsByTypeResponse response;
      return store->GetExecutionsByType(request, &response);
    }
    case ReadNodesByPropertiesConfig::CONTEXTS_BY_TYPE: {
      auto request = absl::get<GetContextsByTypeRequest>(
          work_items_[work_items_index].first);
      GetContextsByTypeResponse response;
      return store->GetContextsByType(request, &response);
    }
    case ReadNodesByPropertiesConfig::ARTIFACT_BY_TYPE_AND_NAME: {
      auto request = absl::get<GetArtifactByTypeAndNameRequest>(
          work_items_[work_items_index].first);
      GetArtifactByTypeAndNameResponse response;
      return store->GetArtifactByTypeAndName(request, &response);
    }
    case ReadNodesByPropertiesConfig::EXECUTION_BY_TYPE_AND_NAME: {
      auto request = absl::get<GetExecutionByTypeAndNameRequest>(
          work_items_[work_items_index].first);
      GetExecutionByTypeAndNameResponse response;
      return store->GetExecutionByTypeAndName(request, &response);
    }
    case ReadNodesByPropertiesConfig::CONTEXT_BY_TYPE_AND_NAME: {
      auto request = absl::get<GetContextByTypeAndNameRequest>(
          work_items_[work_items_index].first);
      GetContextByTypeAndNameResponse response;
      return store->GetContextByTypeAndName(request, &response);
    }
    case ReadNodesByPropertiesConfig::ARTIFACTS_BY_URI: {
      auto request = absl::get<GetArtifactsByURIRequest>(
          work_items_[work_items_index].first);
      GetArtifactsByURIResponse response;
      return store->GetArtifactsByURI(request, &response);
    }
    default:
      return tensorflow::errors::InvalidArgument("Wrong specification!");
  }
}

tensorflow::Status ReadNodesByProperties::TearDownImpl() {
  work_items_.clear();
  return tensorflow::Status::OK();
}

std::string ReadNodesByProperties::GetName() { return name_; }

}  // namespace ml_metadata
