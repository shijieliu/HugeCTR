/*
 * Copyright (c) 2023, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <layer.hpp>
#include <set>
#include <vector>

namespace HugeCTR {

template <typename T>
class GatherLayer : public Layer {
  /*
   * stores the weight tensors of this layer.
   */
  Tensors2<T> weights_;
  /*
   * stores the weight gradient tensors of this layer.
   */
  Tensors2<T> wgrad_;
  /*
   * stores the references to the input tensors of this layer.
   */
  Tensors2<T> in_tensors_;
  /*
   * stores the references to the output tensors of this layer.
   */
  Tensors2<T> out_tensor_;

  // TODO: remove later
  Tensors2<T>& get_in_tensors(bool is_train) { return in_tensors_; }

 public:
  GatherLayer(const core23::Tensor& input_tensor, core23::Tensor& output_tensor,
              std::vector<int>& indices, const std::shared_ptr<GPUResource>& gpu_resource);
  /**
   * Ctor of GatherLayer.
   * @param in_tensor input tensor
   * @param out_tensor vector where the pointers to the created output tensors are stored
   * @param blobs_buff GeneralBuffer used to create the output tensor
   * @param indices set of the Gather indices along dimensions
   * @param device_id the id of GPU where this layer belongs
   */
  GatherLayer(const Tensor2<T>& in_tensor, Tensor2<T>& out_tensor,
              const std::shared_ptr<GeneralBuffer2<CudaAllocator>>& blobs_buff,
              std::vector<int>& indices, const std::shared_ptr<GPUResource>& gpu_resource);
  void initialize() override;
  /**
   * Gather's foward pass to gather data to the output tensor
   * @param stream CUDA stream where the foward propagation is executed
   */
  void fprop(bool is_train) override;
  /**
   * Gather's backward pass to scatter data to the input tensors
   * @param stream CUDA stream where the foward propagation is executed
   */
  void bprop() override;

 private:
  size_t tensor_size_;
  size_t num_indices_;
  std::vector<int> h_indices_;
  core23::Tensor indices23_;
  // TODO: remove later
  Tensor2<int> indices_;
};

}  // namespace HugeCTR
