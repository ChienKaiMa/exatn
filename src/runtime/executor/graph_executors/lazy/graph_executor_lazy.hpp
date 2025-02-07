/** ExaTN:: Tensor Runtime: Tensor graph executor: Lazy
REVISION: 2022/01/08

Copyright (C) 2018-2022 Dmitry Lyakh, Alex McCaskey
Copyright (C) 2018-2022 Oak Ridge National Laboratory (UT-Battelle)

Rationale:

**/

#ifndef EXATN_RUNTIME_LAZY_GRAPH_EXECUTOR_HPP_
#define EXATN_RUNTIME_LAZY_GRAPH_EXECUTOR_HPP_

#include "tensor_graph_executor.hpp"

namespace exatn {
namespace runtime {

#ifdef CUQUANTUM
class CuQuantumExecutor;
#endif

class LazyGraphExecutor : public TensorGraphExecutor {

public:

  static constexpr const unsigned int DEFAULT_PIPELINE_DEPTH = 16;
  static constexpr const unsigned int DEFAULT_PREFETCH_DEPTH = 4;
#ifdef CUQUANTUM
  static constexpr const unsigned int CUQUANTUM_PIPELINE_DEPTH = 2;
#endif

  LazyGraphExecutor(): pipeline_depth_(DEFAULT_PIPELINE_DEPTH),
                       prefetch_depth_(DEFAULT_PREFETCH_DEPTH)
#ifdef CUQUANTUM
                      ,cuquantum_pipe_depth_(CUQUANTUM_PIPELINE_DEPTH)
#endif
  {
  }

  //LazyGraphExecutor(const LazyGraphExecutor &) = delete;
  //LazyGraphExecutor & operator=(const LazyGraphExecutor &) = delete;
  //LazyGraphExecutor(LazyGraphExecutor &&) = delete;
  //LazyGraphExecutor & operator=(LazyGraphExecutor &&) = delete;

  virtual ~LazyGraphExecutor() = default;

  /** Sets/resets the DAG node executor (tensor operation executor). **/
  virtual void resetNodeExecutor(std::shared_ptr<TensorNodeExecutor> node_executor,
                                 const ParamConf & parameters,
                                 unsigned int num_processes,
                                 unsigned int process_rank,
                                 unsigned int global_process_rank) override;

  /** Traverses the DAG and executes all its nodes. **/
  virtual void execute(TensorGraph & dag) override;

  /** Traverses the list of tensor networks and executes them as a whole. **/
  virtual void execute(TensorNetworkQueue & tensor_network_queue) override;

  /** Regulates the tensor prefetch depth (0 turns prefetch off). **/
  virtual void setPrefetchDepth(unsigned int depth) override {
    prefetch_depth_ = depth;
    return;
  }

  /** Returns the current prefetch depth. **/
  inline unsigned int getPrefetchDepth() const {
    return prefetch_depth_;
  }

  /** Returns the current pipeline depth. **/
  inline unsigned int getPipelineDepth() const {
    return pipeline_depth_;
  }

  /** Returns the current value of the total Flop count executed by the node executor. **/
  virtual double getTotalFlopCount() const override;

  const std::string name() const override {return "lazy-dag-executor";}
  const std::string description() const override {return "Lazy tensor graph executor";}
  std::shared_ptr<TensorGraphExecutor> clone() override {return std::make_shared<LazyGraphExecutor>();}

protected:

  unsigned int pipeline_depth_; //max number of active tensor operations in flight
  unsigned int prefetch_depth_; //max number of tensor operations with active prefetch in flight
#ifdef CUQUANTUM
  unsigned int cuquantum_pipe_depth_; //max number of actively executed tensor networks via cuQuantum
  std::shared_ptr<CuQuantumExecutor> cuquantum_executor_; //cuQuantum executor
#endif
};

} //namespace runtime
} //namespace exatn

#endif //EXATN_RUNTIME_LAZY_GRAPH_EXECUTOR_HPP_
