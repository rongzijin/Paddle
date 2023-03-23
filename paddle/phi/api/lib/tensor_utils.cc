/* Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/phi/api/include/tensor_utils.h"

#include "paddle/phi/api/lib/api_registry.h"
#include "paddle/phi/core/dense_tensor.h"

#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
#ifdef PADDLE_WITH_CUDA
#include <cuda_runtime.h>
#else
#include <hip/hip_runtime.h>
#endif
#endif

namespace paddle {

PD_REGISTER_API(from_blob)

phi::Place GetPlaceFromPtr(void* data) {
#if defined(PADDLE_WITH_CUDA) || defined(PADDLE_WITH_HIP)
#ifdef PADDLE_WITH_CUDA
#if CUDA_VERSION >= 10000
  cudaPointerAttributes attr;
  cudaError_t status = cudaPointerGetAttributes(&attr, data);
  if (status == cudaSuccess && attr.type == cudaMemoryTypeDevice) {
    return phi::GPUPlace(attr.device);
  }
#else
  PADDLE_THROW(
      phi::errors::Unimplemented("The GetPlaceFromPtr() method is only "
                                 "supported when CUDA version >= 10.0."));
#endif
#else
  hipPointerAttribute_t attr;
  hipError_t status = hipPointerGetAttributes(&attr, data);
  if (status == hipSuccess && attr.memoryType == hipMemoryTypeDevice) {
    return phi::GPUPlace(attr.device);
  }
#endif
#endif
  return phi::CPUPlace();
}

using AllocationDeleter = void (*)(phi::Allocation*);

PADDLE_API Tensor from_blob(void* data,
                            const phi::IntArray& shape,
                            phi::DataType dtype,
                            phi::DataLayout layout,
                            const phi::Place& place,
                            const Deleter& deleter) {
  PADDLE_ENFORCE_NOT_NULL(
      data, phi::errors::InvalidArgument("data can not be nullptr."));

  PADDLE_ENFORCE_EQ(shape.FromTensor(),
                    false,
                    phi::errors::InvalidArgument(
                        "shape cannot be constructed from a Tensor."));

  phi::Place data_place;
  if (place.GetType() == phi::AllocationType::UNDEFINED ||
      place.GetType() == phi::AllocationType::CPU ||
      place.GetType() == phi::AllocationType::GPU) {
    data_place = GetPlaceFromPtr(data);
    if (place.GetType() != phi::AllocationType::UNDEFINED) {
      PADDLE_ENFORCE_EQ(data_place,
                        place,
                        phi::errors::InvalidArgument(
                            "Specified place does not match place of data. ",
                            "Specified: %s, Exptected: %s.",
                            data_place.DebugString(),
                            place.DebugString()));
    }
  } else {
    data_place = place;
  }

  auto meta =
      phi::DenseTensorMeta(dtype, phi::make_ddim(shape.GetData()), layout);

  size_t size = SizeOf(dtype) * (meta.is_scalar ? 1 : product(meta.dims));

  AllocationDeleter alloc_deleter = nullptr;
  if (deleter) {
    static thread_local Deleter g_deleter = deleter;
    alloc_deleter = [](phi::Allocation* p) { g_deleter(p->ptr()); };
  }

  auto alloc =
      std::make_shared<phi::Allocation>(data, size, alloc_deleter, data_place);

  return Tensor(std::make_shared<phi::DenseTensor>(alloc, meta));
}

}  // namespace paddle