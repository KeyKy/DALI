// Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_PIPELINE_EXECUTOR_ASYNC_PIPELINED_EXECUTOR_H_
#define DALI_PIPELINE_EXECUTOR_ASYNC_PIPELINED_EXECUTOR_H_

#include <string>
#include "dali/common.h"
#include "dali/error_handling.h"
#include "dali/pipeline/executor/pipelined_executor.h"
#include "dali/pipeline/util/worker_thread.h"

namespace dali {

/**
 * @brief This executor uses worker threads to pipelined the
 * issue of cpu, mixed, and gpu work. Calls the RunCPU,
 * RunMixed, and RunGPU are all asynchronous, and results
 * are retrieved by calling Outputs, which manages all
 * needed synchronization.
 */
class AsyncPipelinedExecutor : public PipelinedExecutor {
 public:
  inline AsyncPipelinedExecutor(int batch_size, int num_thread,
      int device_id, size_t bytes_per_sample_hint,
      bool set_affinity = false, int max_num_stream = -1) :
    PipelinedExecutor(batch_size, num_thread, device_id,
        bytes_per_sample_hint, set_affinity, max_num_stream),
    cpu_thread_(device_id, set_affinity),
    mixed_thread_(device_id, set_affinity),
    gpu_thread_(device_id, set_affinity),
    device_id_(device_id) {}

  virtual ~AsyncPipelinedExecutor() {
    cpu_thread_.ForceStop();
    mixed_thread_.ForceStop();
    gpu_thread_.ForceStop();
  }

  void Init() override {
    if (!cpu_thread_.WaitForInit()
        || !mixed_thread_.WaitForInit()
        || !gpu_thread_.WaitForInit()) {
      cpu_thread_.ForceStop();
      mixed_thread_.ForceStop();
      gpu_thread_.ForceStop();
      std::string error = "Failed to init pipeline on device " + std::to_string(device_id_);
      throw std::runtime_error(error);
    }
  }

  void RunCPU() override;

  void RunMixed() override;

  void RunGPU() override;

  void Outputs(DeviceWorkspace *ws) override {
    CheckForErrors();
    try {
      PipelinedExecutor::Outputs(ws);
    } catch (std::runtime_error &e) {
      exec_error_ = true;
      mixed_work_cv_.notify_all();
      gpu_work_cv_.notify_all();
      throw std::runtime_error(std::string(e.what()));
    } catch (...) {
      throw std::runtime_error("Unknown critical error in pipeline");
    }
  }

 protected:
  void CheckForErrors() {
    cpu_thread_.CheckForErrors();
    mixed_thread_.CheckForErrors();
    gpu_thread_.CheckForErrors();
  }

  WorkerThread cpu_thread_, mixed_thread_, gpu_thread_;
  int cpu_work_counter_ = 0, mixed_work_counter_ = 0, gpu_work_counter_ = 0;
  std::mutex cpu_mutex_, mixed_mutex_, gpu_mutex_;
  std::condition_variable mixed_work_cv_, gpu_work_cv_;
  int device_id_;
};

}  // namespace dali

#endif  // DALI_PIPELINE_EXECUTOR_ASYNC_PIPELINED_EXECUTOR_H_
