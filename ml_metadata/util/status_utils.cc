/* Copyright 2021 Google LLC

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
#include "ml_metadata/util/status_utils.h"

#include "absl/status/status.h"
#include "tensorflow/core/lib/core/status.h"

namespace ml_metadata {

tensorflow::Status FromABSLStatus(const absl::Status& s) {
  if (s.ok()) {
    return tensorflow::Status();
  }
  // The string types may differ between std::string and ::string, so do an
  // explicit conversion.
  return tensorflow::Status(static_cast<tensorflow::error::Code>(s.code()),
                            s.message());
}

}  // namespace ml_metadata
