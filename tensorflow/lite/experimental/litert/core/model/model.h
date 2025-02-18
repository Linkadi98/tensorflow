// Copyright 2024 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TENSORFLOW_LITE_EXPERIMENTAL_LITERT_CORE_MODEL_MODEL_H_
#define TENSORFLOW_LITE_EXPERIMENTAL_LITERT_CORE_MODEL_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "tensorflow/lite/experimental/litert/c/litert_common.h"
#include "tensorflow/lite/experimental/litert/c/litert_model.h"
#include "tensorflow/lite/experimental/litert/c/litert_op_code.h"
#include "tensorflow/lite/experimental/litert/cc/litert_buffer_ref.h"
#include "tensorflow/lite/experimental/litert/cc/litert_expected.h"
#include "tensorflow/lite/schema/schema_generated.h"

//
// Tensor
//

struct LiteRtWeightsT {
  std::unique_ptr<tflite::BufferT> fb_buffer = nullptr;
};

typedef union {
  LiteRtUnrankedTensorType unranked_tensor_type;
  LiteRtRankedTensorType ranked_tensor_type;
} LiteRtTypeDetail;

using TensorType = std::pair<LiteRtTensorTypeId, LiteRtTypeDetail>;

typedef union {
  LiteRtQuantizationPerTensor per_tensor;
} LiteRtQuantizationTypeDetail;

using Quantization =
    std::pair<LiteRtQuantizationTypeId, LiteRtQuantizationTypeDetail>;

struct LiteRtTensorT {
  using Ref = std::reference_wrapper<LiteRtTensorT>;

  // Empty if subgraph output. This is a reference.
  std::vector<LiteRtOp> users;

  // Which arg number for user i.
  std::vector<LiteRtParamIndex> user_arg_inds;

  // Null if subgraph input or constant. This is a reference.
  LiteRtOp defining_op = nullptr;

  // Which output ind from defining op made this tensor.
  LiteRtParamIndex defining_op_out_ind;

  // Not a reference.
  LiteRtWeightsT weights;

  // Id for union tensor type.
  LiteRtTensorTypeId type_id;

  // Union tensor type.
  LiteRtTypeDetail type_detail;

  // Id for union quantization type.
  LiteRtQuantizationTypeId q_type_id = kLiteRtQuantizationNone;

  // Union quantization type.
  LiteRtQuantizationTypeDetail q_type_detail;

  // Authored name of tensor, may be empty.
  std::string name;

 private:
  // TODO Unify mangement of dims and clean this up.
  litert::SmallVec<int32_t> dims;
};

//
// Op
//

struct LiteRtOpT {
  // These are references.
  std::vector<LiteRtTensor> inputs;

  // These are references.
  std::vector<LiteRtTensor> outputs;

  LiteRtOpCode op_code;

  litert::OwningBufferRef<uint8_t> custom_options;

  tflite::BuiltinOptionsUnion option;

  // Add a new input to this op and updating given tensors users.
  void AddInput(LiteRtTensorT& input_tensor) {
    input_tensor.users.push_back(this);
    input_tensor.user_arg_inds.push_back(inputs.size());
    inputs.push_back(&input_tensor);
  }

  // Add a new output to this op and update given tensors defining op.
  void AddOutput(LiteRtTensorT& output_tensor) {
    output_tensor.defining_op_out_ind = outputs.size();
    output_tensor.defining_op = this;
    outputs.push_back(&output_tensor);
  }
};

//
// Subgraph
//

struct LiteRtSubgraphT {
  // Storage and views of tensors. Clients are only shown views. Facilitates
  // efficient topological mutation.
  std::list<LiteRtTensorT> tensors_storage;
  std::vector<LiteRtTensor> tensors;

  // Storage and vies of ops.
  std::list<LiteRtOpT> ops_storage;
  std::vector<LiteRtOp> ops;

  // Shared view of initial flatbuffer data.
  std::shared_ptr<tflite::SubGraphT> flatbuffer_subgraph;

  // These are references and a subset of `tensors`.
  std::vector<LiteRtTensor> inputs;

  // These are references and a subset of `tensors`.
  std::vector<LiteRtTensor> outputs;

  LiteRtTensorT& EmplaceTensor() {
    auto& tensor = tensors_storage.emplace_back();
    tensors.push_back(&tensor);
    return tensor;
  }

  LiteRtOpT& EmplaceOp() {
    auto& op = ops_storage.emplace_back();
    ops.push_back(&op);
    return op;
  }
};

//
// Signature
//

#define LITERT_DEFAULT_SIGNATURE_KEY "<placeholder signature>"

struct LiteRtSignatureT {
  using Ptr = std::unique_ptr<LiteRtSignatureT>;
  absl::string_view key;
  int subgraph_index;
  std::vector<absl::string_view> input_names;
  std::vector<absl::string_view> output_names;
};

//
// Model
//

// A (partial) unpacking of the flatbuffer model into a list of subgraphs.
// Keeps a reference to the flatbuffer model. Lifetimes of all storage
// are linked to the containing model.
struct LiteRtModelT {
  using Ref = std::reference_wrapper<LiteRtModelT>;

  // Subgraphs that have been unpacked into usable types.
  std::vector<LiteRtSubgraphT> subgraphs;

  // Initial flatbuffer loaded in. "Subgraphs" field has been invalidated.
  std::unique_ptr<tflite::ModelT> flatbuffer_model;

  // Custom code associated with all customs ops emitted during
  // re-serialization.
  std::string custom_op_code;

  // Signature definitions.
  std::vector<std::unique_ptr<LiteRtSignatureT>> signatures;

  // Look up metadata by key, getting a view of its buffer as a string
  // if it exists.
  litert::Expected<litert::BufferRef<uint8_t>> FindMetadata(
      absl::string_view key) const;

  // Adds a new metadata buffer to the model. Fails if it already exists.
  LiteRtStatus PushMetadata(absl::string_view key,
                            litert::BufferRef<uint8_t> data);

  // Look up signature by key.
  litert::Expected<LiteRtSignatureT*> FindSignature(
      absl::string_view signature_key) const;

  // Look up subgraph by key.
  litert::Expected<const LiteRtSubgraphT*> FindSubgraph(
      absl::string_view signature_key) const;

  size_t MainSubgraphIndex() const {
    // TODO replace this with the index of the default signature.
    return 0;
  }

  const LiteRtSubgraphT& MainSubgraph() const {
    return subgraphs[MainSubgraphIndex()];
  }
};

//
// Utils
//

// Used for communicating selections of ops.
class LiteRtOpListT {
 public:
  void Push(LiteRtOp op) { ops_.push_back(op); }

  std::vector<LiteRtOp> Vec() const {
    std::vector<LiteRtOp> res;
    res.reserve(ops_.size());
    res.assign(ops_.begin(), ops_.end());
    return res;
  }

 private:
  // NOTE: This was originally a vector. Was encountering really odd
  // segfaults when freeing after code on another side of a compilation boundary
  // was doing pushes that resized. A list+copy to vector is not optimimal,
  // revisit if bottleneck.
  std::list<LiteRtOp> ops_;
};

#endif  // TENSORFLOW_LITE_EXPERIMENTAL_LITERT_CORE_MODEL_MODEL_H_
