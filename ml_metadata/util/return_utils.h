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
#ifndef THIRD_PARTY_ML_METADATA_UTIL_RETURN_UTILS_H_
#define THIRD_PARTY_ML_METADATA_UTIL_RETURN_UTILS_H_

namespace ml_metadata {

// For propagating errors when calling a function.
#define MLMD_RETURN_IF_ERROR(...)         \
  do {                                    \
    absl::Status _status = (__VA_ARGS__); \
    if (!_status.ok()) return _status;    \
  } while (0)

#define MLMD_RETURN_WITH_CONTEXT_IF_ERROR(expr, ...)                     \
  do {                                                                   \
    absl::Status _status = (expr);                                       \
    if (!_status.ok()) {                                                 \
      return absl::Status(_status.code(),                                \
                          absl::StrCat(__VA_ARGS__, _status.message())); \
    }                                                                    \
  } while (0)

}  // namespace ml_metadata

#endif  // THIRD_PARTY_ML_METADATA_UTIL_RETURN_UTILS_H_
