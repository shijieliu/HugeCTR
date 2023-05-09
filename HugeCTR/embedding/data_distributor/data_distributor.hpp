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

#include <nccl.h>

#include <core/core.hpp>
#include <embedding/common.hpp>
#include <embedding/data_distributor/key_filtering_operators.hpp>
#include <embedding/operators/compress_offset.hpp>
#include <embedding/operators/dp_index_calculation.hpp>
#include <embedding/operators/keys_to_indices.hpp>
#include <embedding/operators/mp_index_calculation.hpp>
#include <embedding/operators/transpose_input.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

namespace HugeCTR {

class DataDistributor {
 public:
  using Result = std::vector<embedding::EmbeddingInput>;

  DataDistributor(std::vector<std::shared_ptr<core::CoreResourceManager>>& core_resource_managers,
                  const embedding::EmbeddingCollectionParam& ebc_param,
                  const std::vector<embedding::EmbeddingTableParam>& emb_table_param_list);

  void distribute(int gpu_id, const std::vector<core23::Tensor>& dp_keys,
                  const std::vector<core23::Tensor>& dp_bucket_range, Result& output,
                  int batch_size);

  void distribute(int gpu_id, const core23::Tensor& fullbatch_keys,
                  const core23::Tensor& fullbatch_bucket_range, Result& output, int batch_size);

  // TODO: remove when enable table filtering. This function is just to set the bucket ranges
  // because we return a global batch
  void init_fixed_bucket_ranges(core23::Tensor& output_bucket_ranges) const;

 private:
  struct GpuCommData {
    // This is a performance optimization to prevent us from computing bucket ranges each iteration.
    // If the current_batch_size == last_batch_size then the bucket_ranges are the same.
    int last_batch_size;
    core23::Tensor hotness_bucket_range;
    core23::Tensor bucket_range;  // TODO: remove when we no longer need fullbatch_bucket_range
  };

  void key_filtered_distribute(int gpu_id, const std::vector<core23::Tensor>& dp_keys,
                               const std::vector<core23::Tensor>& dp_bucket_range, Result& output,
                               int batch_size);

  size_t feature_id_to_group_id(size_t feature_id) const;

  void init_comm_data();

  void init_batch_major_fullbatch_input_preprocessor();

  void init_indices_converter();

  void init_filtered_all_to_all();

  void all2all_keys_per_bucket(int mp_group_i, int gpu_id);
  void all2all_keys(int mp_group_i, int gpu_id, size_t& received_num_keys);

  std::vector<std::shared_ptr<core::CoreResourceManager>> core_resource_managers_;
  std::vector<int> feature_pooling_factors_;
  std::vector<std::vector<int>> resident_feature_tables_;  // [gpu_id][feature_id]
  std::vector<GpuCommData> gpu_comm_data_;

  struct MPTempStorage {
    MPTempStorage(std::shared_ptr<core::CoreResourceManager> core, int batch_size,
                  int sample_max_nnz, int max_local_features, int max_local_buckets,
                  core23::DataType key_type, core23::DataType offset_type);

    core23::Tensor temp_sort_storage;
    core23::Tensor temp_scan_storage;
    core23::Tensor k_per_b_gpu_major;       // keys-per-bucket
    core23::Tensor k_per_b_feat_major;      // keys-per-bucket
    core23::Tensor k_per_g;                 // keys-per-gpu
    core23::Tensor bucket_range_gpu_major;  // received from nccl
    core23::Tensor sorted_local_keys;
    core23::Tensor sorted_local_labels;
    core23::Tensor keys;  // received from nccl
    void* h_send_k_per_g;
    void* h_recv_k_per_g;
  };

  // Key Filtering (MP)
  std::vector<ComputeDPBucketRangeOperator> compute_dp_bucket_range_operators_;
  std::vector<std::vector<mp::LabelAndCountKeysOperator>> label_and_count_keys_operators_;
  std::vector<std::vector<mp::LabelAndCountKeysOperator::Result>> label_and_count_keys_outputs_;
  std::vector<std::vector<mp::CountKeysOperator>> count_keys_operators_;
  std::vector<std::vector<mp::TransposeBucketsOperator>> transpose_buckets_operators_;
  std::vector<std::vector<mp::SwizzleKeysOperator>> swizzle_keys_operators_;
  std::vector<std::vector<MPTempStorage>> temp_storage_;  // [mp_grouped_i][gpu_id]

  std::vector<std::vector<dp::ConcatKeysAndBucketRangeOperator>>
      concat_keys_and_bucket_range_operators_;

  std::vector<std::vector<core23::Tensor>> fixed_dp_bucket_range_;

  size_t batch_size_;
  size_t batch_size_per_gpu_;
  size_t sample_max_nnz_;

  embedding::EmbeddingCollectionParam ebc_param_;
  std::unordered_map<size_t, size_t> feature_id_to_group_id_map_;
  std::unordered_map<size_t, size_t> feature_id_to_table_id_map_;

  std::vector<embedding::CompressOffset> compress_offsets_;
  std::vector<core23::Tensor> d_local_table_id_lists_;
  std::vector<embedding::EmbeddingTableParam> emb_table_param_list_;
  std::vector<embedding::KeysToIndicesConverter> indices_converters_;

  size_t num_local_gpus_;
  size_t num_global_gpus_;
  size_t num_features_;

  struct KeyFilterInitParams {
    int num_lookup;
    int global_gpu_id;
    int total_gpu_count;

    int num_local_lookup;
    int num_hotness;
    int num_local_hotness;

    core23::Tensor d_local_lookup_ids;
    core23::Tensor d_local_shard_ids;
    core23::Tensor d_local_num_shards;

    KeyFilterInitParams(const std::shared_ptr<core::CoreResourceManager>& core_resource_manager,
                        const embedding::EmbeddingCollectionParam& ebc_param, size_t grouped_id);
  };
  std::vector<std::vector<KeyFilterInitParams>> key_filters_init_params_;

  struct KeyFilter {
    embedding::MPKeySelector mp_key_selector;
    embedding::ModelIndexCalculation mp_index_calculation;
    embedding::DPKeySelector dp_key_selector;
    embedding::DPIndexCalculation dp_index_calculation;
  };
  std::vector<std::vector<KeyFilter>> key_filters_;

  void init_key_filter();

  std::vector<std::unique_ptr<embedding::PreprocessInput>> preprocess_inputs_;
};

DataDistributor::Result allocate_output_for_data_distributor(
    std::shared_ptr<core::CoreResourceManager>& core_resource_manager,
    const embedding::EmbeddingCollectionParam& ebc_param);
}  // namespace HugeCTR
