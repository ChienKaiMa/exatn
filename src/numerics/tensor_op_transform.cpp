/** ExaTN::Numerics: Tensor operation: Transforms/initializes a tensor
REVISION: 2021/07/15

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

#include "exatn_service.hpp"

#include "tensor_op_transform.hpp"

#include "tensor_node_executor.hpp"

namespace exatn{

namespace numerics{

TensorOpTransform::TensorOpTransform():
 TensorOperation(TensorOpCode::TRANSFORM,1,1,1,{0})
{
 this->setScalar(0,std::complex<double>{0.0,0.0}); //default numerical initialization value
}

bool TensorOpTransform::isSet() const
{
 return (this->getNumOperandsSet() == this->getNumOperands());
}

int TensorOpTransform::accept(runtime::TensorNodeExecutor & node_executor,
                              runtime::TensorOpExecHandle * exec_handle)
{
 return node_executor.execute(*this,exec_handle);
}

std::unique_ptr<TensorOperation> TensorOpTransform::createNew()
{
 return std::unique_ptr<TensorOperation>(new TensorOpTransform());
}

std::size_t TensorOpTransform::decompose(const TensorMapper & tensor_mapper)
{
 assert(false);
 //`Implement
 return 0;
}

} //namespace numerics

} //namespace exatn
