/** ExaTN::Numerics: Tensor operation: Transforms/initializes a tensor
REVISION: 2021/07/15

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

/** Rationale:
 (a) Transforms/initializes a tensor inside the processing backend.
     Requires a user-provided talsh::TensorFunctor object to concretize
     the transformation/initilization operation.
**/

#ifndef EXATN_NUMERICS_TENSOR_OP_TRANSFORM_HPP_
#define EXATN_NUMERICS_TENSOR_OP_TRANSFORM_HPP_

#include "Identifiable.hpp"

#include "tensor_basic.hpp"
#include "tensor_operation.hpp"

#include "tensor_method.hpp"

namespace exatn{

namespace numerics{

class TensorOpTransform: public TensorOperation{
public:

 TensorOpTransform();

 TensorOpTransform(const TensorOpTransform &) = default;
 TensorOpTransform & operator=(const TensorOpTransform &) = default;
 TensorOpTransform(TensorOpTransform &&) noexcept = default;
 TensorOpTransform & operator=(TensorOpTransform &&) noexcept = default;
 virtual ~TensorOpTransform() = default;

 virtual std::unique_ptr<TensorOperation> clone() const override{
  return std::unique_ptr<TensorOperation>(new TensorOpTransform(*this));
 }

 /** Returns TRUE iff the tensor operation is fully set. **/
 virtual bool isSet() const override;

 /** Accepts tensor node executor which will execute this tensor operation. **/
 virtual int accept(runtime::TensorNodeExecutor & node_executor,
                    runtime::TensorOpExecHandle * exec_handle) override;

 /** Decomposes a composite tensor operation into simple ones.
     Returns the total number of generated simple operations. **/
 virtual std::size_t decompose(const TensorMapper & tensor_mapper) override;

 /** Create a new polymorphic instance of this subclass. **/
 static std::unique_ptr<TensorOperation> createNew();

 void resetFunctor(std::shared_ptr<talsh::TensorFunctor<Identifiable>> functor){
  functor_ = functor;
  return;
 }

 int apply(talsh::Tensor & local_tensor){
  if(functor_) return functor_->apply(local_tensor);
  return 0;
 }

private:

 std::shared_ptr<talsh::TensorFunctor<Identifiable>> functor_; //tensor functor (method)
};

} //namespace numerics

} //namespace exatn

#endif //EXATN_NUMERICS_TENSOR_OP_TRANSFORM_HPP_
