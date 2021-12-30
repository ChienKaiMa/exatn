/** ExaTN: Tensor Runtime: Tensor network executor: NVIDIA cuQuantum
REVISION: 2021/12/30

Copyright (C) 2018-2021 Dmitry Lyakh
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle)

Rationale:

**/

#ifdef CUQUANTUM

#include <cutensornet.h>
#include <cutensor.h>
#include <cuda_runtime.h>

#include <vector>
#include <unordered_map>
#include <type_traits>

#include <iostream>

#include "talshxx.hpp"

#include "cuquantum_executor.hpp"


#define HANDLE_CUDA_ERROR(x) \
{ const auto err = x; \
  if( err != cudaSuccess ) \
{ printf("Error: %s in line %d\n", cudaGetErrorString(err), __LINE__); std::abort(); } \
};

#define HANDLE_CTN_ERROR(x) \
{ const auto err = x; \
  if( err != CUTENSORNET_STATUS_SUCCESS ) \
{ printf("Error: %s in line %d\n", cutensornetGetErrorString(err), __LINE__); std::abort(); } \
};


namespace exatn {
namespace runtime {

struct TensorDescriptor {
 std::vector<int64_t> extents; //tensor dimension extents
 std::vector<int64_t> strides; //tensor dimension strides (optional)
 cudaDataType_t data_type;     //tensor element data type
 std::size_t volume = 0;       //tensor body volume
 std::size_t size = 0;         //tensor body size (bytes)
 void * src_ptr = nullptr;     //non-owning pointer to the tensor body source image
 std::vector<void*> dst_ptr;   //non-owning pointer to the tensor body dest image (for all GPU)
};

struct TensorNetworkReq {
 TensorNetworkQueue::ExecStat exec_status = TensorNetworkQueue::ExecStat::None; //tensor network execution status
 std::shared_ptr<numerics::TensorNetwork> network; //tensor network specification
 std::unordered_map<numerics::TensorHashType,TensorDescriptor> tensor_descriptors; //tensor descriptors (shape, volume, data type, body)
 std::unordered_map<unsigned int, std::vector<int32_t>> tensor_modes; //indices associated with tensor dimensions (key is the original tensor id)
 std::unordered_map<int32_t,int64_t> mode_extents; //extent of each registered tensor mode
 int32_t * num_modes_in = nullptr;
 int64_t ** extents_in = nullptr;
 int64_t ** strides_in = nullptr;
 int32_t ** modes_in = nullptr;
 uint32_t * alignments_in = nullptr;
 int32_t num_modes_out;
 int64_t * extents_out = nullptr;
 int64_t * strides_out = nullptr;
 int32_t * modes_out = nullptr;
 uint32_t alignment_out;
 std::vector<void*> memory_window_ptr; //end of the GPU memory segment allocated for the tensors
 cutensornetNetworkDescriptor_t net_descriptor;
 cutensornetContractionOptimizerConfig_t opt_config;
 cutensornetContractionOptimizerInfo_t opt_info;
 cutensornetContractionPlan_t comp_plan;
 cudaDataType_t data_type;
 cutensornetComputeType_t compute_type;
 cudaStream_t stream;
};


CuQuantumExecutor::CuQuantumExecutor(TensorImplFunc tensor_data_access_func):
 tensor_data_access_func_(std::move(tensor_data_access_func))
{
 static_assert(std::is_same<cutensornetHandle_t,void*>::value,"#FATAL(exatn::runtime::CuQuantumExecutor): cutensornetHandle_t != (void*)");

 const size_t version = cutensornetGetVersion();
 std::cout << "#DEBUG(exatn::runtime::CuQuantumExecutor): cuTensorNet backend version " << version << std::endl;

 int num_gpus = 0;
 auto error_code = talshDeviceCount(DEV_NVIDIA_GPU,&num_gpus); assert(error_code == TALSH_SUCCESS);
 for(int i = 0; i < num_gpus; ++i){
  if(talshDeviceState(i,DEV_NVIDIA_GPU) >= DEV_ON){
   gpu_attr_.emplace_back(std::make_pair(i,DeviceAttr{}));
   gpu_attr_.back().second.workspace_ptr = talsh::getDeviceBufferBasePtr(DEV_NVIDIA_GPU,i);
   assert(reinterpret_cast<std::size_t>(gpu_attr_.back().second.workspace_ptr) % MEM_ALIGNMENT == 0);
   gpu_attr_.back().second.buffer_size = talsh::getDeviceMaxBufferSize(DEV_NVIDIA_GPU,i);
   std::size_t wrk_size = (std::size_t)(static_cast<float>(gpu_attr_.back().second.buffer_size) * WORKSPACE_FRACTION);
   wrk_size -= wrk_size % MEM_ALIGNMENT;
   gpu_attr_.back().second.workspace_size = wrk_size;
   gpu_attr_.back().second.buffer_size -= wrk_size;
   gpu_attr_.back().second.buffer_size -= gpu_attr_.back().second.buffer_size % MEM_ALIGNMENT;
   gpu_attr_.back().second.buffer_ptr = (void*)(((char*)(gpu_attr_.back().second.workspace_ptr)) + wrk_size);
   mem_pool_.emplace_back(LinearMemoryPool(gpu_attr_.back().second.buffer_ptr,
                                           gpu_attr_.back().second.buffer_size,MEM_ALIGNMENT));
  }
 }
 std::cout << "#DEBUG(exatn::runtime::CuQuantumExecutor): Number of available GPUs = " << gpu_attr_.size() << std::endl;

 for(const auto & gpu: gpu_attr_){
  HANDLE_CUDA_ERROR(cudaSetDevice(gpu.first));
  HANDLE_CTN_ERROR(cutensornetCreate((cutensornetHandle_t*)(&gpu.second.cutn_handle)));
 }
 std::cout << "#DEBUG(exatn::runtime::CuQuantumExecutor): Created cuTensorNet contexts for all available GPUs" << std::endl;

 std::cout << "#DEBUG(exatn::runtime::CuQuantumExecutor): GPU configuration:\n";
 for(const auto & gpu: gpu_attr_){
  std::cout << " GPU #" << gpu.first
            << ": wrk_ptr = " << gpu.second.workspace_ptr
            << ", size = " << gpu.second.workspace_size
            << "; buf_ptr = " << gpu.second.buffer_ptr
            << ", size = " << gpu.second.buffer_size << std::endl;
 }
}


CuQuantumExecutor::~CuQuantumExecutor()
{
 sync();
 for(const auto & gpu: gpu_attr_){
  HANDLE_CUDA_ERROR(cudaSetDevice(gpu.first));
  HANDLE_CTN_ERROR(cutensornetDestroy((cutensornetHandle_t)(gpu.second.cutn_handle)));
 }
 std::cout << "#DEBUG(exatn::runtime::CuQuantumExecutor): Destroyed cuTensorNet contexts for all available GPUs" << std::endl;
 gpu_attr_.clear();
}


TensorNetworkQueue::ExecStat CuQuantumExecutor::execute(std::shared_ptr<numerics::TensorNetwork> network,
                                                        const TensorOpExecHandle exec_handle)
{
 assert(network);
 TensorNetworkQueue::ExecStat exec_stat = TensorNetworkQueue::ExecStat::None;
 auto res = active_networks_.emplace(std::make_pair(exec_handle, new TensorNetworkReq{}));
 if(res.second){
  auto tn_req = res.first->second;
  tn_req->network = network;
  tn_req->exec_status = TensorNetworkQueue::ExecStat::Idle;
  parseTensorNetwork(tn_req); //still Idle
  loadTensors(tn_req); //Idle --> Loading
  if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Loading){
   planExecution(tn_req); //Loading --> Planning (while loading data)
   if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Planning){
    contractTensorNetwork(tn_req); //Planning --> Executing
   }
  }
  exec_stat = tn_req->exec_status;
 }else{
  std::cout << "#WARNING(exatn::runtime::CuQuantumExecutor): execute: Repeated tensor network submission detected!\n";
 }
 return exec_stat;
}


TensorNetworkQueue::ExecStat CuQuantumExecutor::sync(const TensorOpExecHandle exec_handle,
                                                     int * error_code)
{
 *error_code = 0;
 TensorNetworkQueue::ExecStat exec_stat = TensorNetworkQueue::ExecStat::None;
 auto iter = active_networks_.find(exec_handle);
 if(iter != active_networks_.end()){
  auto tn_req = iter->second;
  if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Executing){
   testCompletion(tn_req); //Executing --> Completed
  }else{
   if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Idle)
    loadTensors(tn_req); //Idle --> Loading
   if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Loading)
    planExecution(tn_req); //Loading --> Planning (while loading data)
   if(tn_req->exec_status == TensorNetworkQueue::ExecStat::Planning)
    contractTensorNetwork(tn_req); //Planning --> Executing
  }
  exec_stat = tn_req->exec_status;
  tn_req.reset();
  if(exec_stat == TensorNetworkQueue::ExecStat::Completed)
   active_networks_.erase(iter);
 }
 return exec_stat;
}


void CuQuantumExecutor::sync()
{
 while(!active_networks_.empty()){
  for(auto iter = active_networks_.begin(); iter != active_networks_.end(); ++iter){
   int error_code = 0;
   const auto exec_stat = sync(iter->first,&error_code); assert(error_code == 0);
   if(exec_stat == TensorNetworkQueue::ExecStat::Completed) break;
  }
 }
 return;
}


cudaDataType_t getCudaDataType(const TensorElementType elem_type)
{
 cudaDataType_t cuda_data_type;
 switch(elem_type){
 case TensorElementType::REAL32: cuda_data_type = CUDA_R_32F; break;
 case TensorElementType::REAL64: cuda_data_type = CUDA_R_64F; break;
 case TensorElementType::COMPLEX32: cuda_data_type = CUDA_C_32F; break;
 case TensorElementType::COMPLEX64: cuda_data_type = CUDA_C_64F; break;
 default:
  assert(false);
 }
 return cuda_data_type;
}


void CuQuantumExecutor::parseTensorNetwork(std::shared_ptr<TensorNetworkReq> tn_req)
{
 const auto & net = *(tn_req->network);
 const int32_t num_input_tensors = net.getNumTensors();
 tn_req->num_modes_in = new int32_t[num_input_tensors];
 tn_req->extents_in = new int64_t*[num_input_tensors];
 tn_req->strides_in = new int64_t*[num_input_tensors];
 tn_req->modes_in = new int32_t*[num_input_tensors];
 tn_req->alignments_in = new uint32_t[num_input_tensors];

 for(unsigned int i = 0; i < num_input_tensors; ++i) tn_req->strides_in[i] = NULL;
 for(unsigned int i = 0; i < num_input_tensors; ++i) tn_req->alignments_in[i] = MEM_ALIGNMENT;
 tn_req->strides_out = NULL;
 tn_req->alignment_out = MEM_ALIGNMENT;

 int32_t mode_id = 0, tens_num = 0;
 for(auto iter = net.cbegin(); iter != net.cend(); ++iter){
  const auto tens_id = iter->first;
  const auto & tens = iter->second;
  const auto tens_hash = tens.getTensor()->getTensorHash();
  const auto tens_vol = tens.getTensor()->getVolume();
  const auto tens_rank = tens.getRank();
  const auto tens_type = tens.getElementType();
  const auto & tens_legs = tens.getTensorLegs();
  const auto & tens_dims = tens.getDimExtents();

  auto res0 = tn_req->tensor_descriptors.emplace(std::make_pair(tens_hash,TensorDescriptor{}));
  if(res0.second){
   auto & descr = res0.first->second;
   descr.extents.resize(tens_rank);
   for(unsigned int i = 0; i < tens_rank; ++i) descr.extents[i] = tens_dims[i];
   descr.data_type = getCudaDataType(tens_type);
   descr.volume = tens_vol;
   descr.src_ptr = tensor_data_access_func_(*(tens.getTensor()),DEV_HOST,0,&(descr.size));
   assert(descr.src_ptr != nullptr);
  }

  auto res1 = tn_req->tensor_modes.emplace(std::make_pair(tens_id,std::vector<int32_t>(tens_rank)));
  assert(res1.second);
  for(unsigned int i = 0; i < tens_rank; ++i){
   const auto other_tens_id = tens_legs[i].getTensorId();
   const auto other_tens_leg_id = tens_legs[i].getDimensionId();
   auto other_tens_iter = tn_req->tensor_modes.find(other_tens_id);
   if(other_tens_iter == tn_req->tensor_modes.end()){
    res1.first->second[i] = ++mode_id;
    auto new_mode = tn_req->mode_extents.emplace(std::make_pair(mode_id,tens_dims[i]));
   }else{
    res1.first->second[i] = other_tens_iter->second[other_tens_leg_id];
   }
  }

  if(tens_id == 0){ //output tensor
   tn_req->num_modes_out = tens_rank;
   tn_req->extents_out = res0.first->second.extents.data();
   tn_req->modes_out = res1.first->second.data();
  }else{ //input tensors
   tn_req->num_modes_in[tens_num] = tens_rank;
   tn_req->extents_in[tens_num] = res0.first->second.extents.data();
   tn_req->modes_in[tens_num] = res1.first->second.data();
   ++tens_num;
  }
 }

 HANDLE_CTN_ERROR(cutensornetCreateNetworkDescriptor(gpu_attr_[0].second.cutn_handle,num_input_tensors,
                  tn_req->num_modes_in,tn_req->extents_in,tn_req->strides_in,tn_req->modes_in,tn_req->alignments_in,
                  tn_req->num_modes_out,tn_req->extents_out,tn_req->strides_out,tn_req->modes_out,tn_req->alignment_out,
                  tn_req->data_type,tn_req->compute_type,&(tn_req->net_descriptor)));

 HANDLE_CUDA_ERROR(cudaStreamCreate(&(tn_req->stream)));
 return;
}


void CuQuantumExecutor::loadTensors(std::shared_ptr<TensorNetworkReq> tn_req)
{
 
 return;
}


void CuQuantumExecutor::planExecution(std::shared_ptr<TensorNetworkReq> tn_req)
{
 
 return;
}


void CuQuantumExecutor::contractTensorNetwork(std::shared_ptr<TensorNetworkReq> tn_req)
{
 
 return;
}


void CuQuantumExecutor::testCompletion(std::shared_ptr<TensorNetworkReq> tn_req)
{
 
 return;
}

} //namespace runtime
} //namespace exatn

#endif //CUQUANTUM
