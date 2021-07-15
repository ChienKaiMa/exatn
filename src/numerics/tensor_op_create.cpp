/** ExaTN::Numerics: Tensor operation: Creates a tensor
REVISION: 2021/07/15

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

#include "exatn_service.hpp"

#include "tensor_op_create.hpp"

#include "tensor_node_executor.hpp"

namespace exatn{

namespace numerics{

TensorOpCreate::TensorOpCreate():
 TensorOperation(TensorOpCode::CREATE,1,0,1,{0}),
 element_type_(TensorElementType::REAL64)
{
}

bool TensorOpCreate::isSet() const
{
 return (this->getNumOperandsSet() == this->getNumOperands());
}

int TensorOpCreate::accept(runtime::TensorNodeExecutor & node_executor,
                           runtime::TensorOpExecHandle * exec_handle)
{
 return node_executor.execute(*this,exec_handle);
}

std::unique_ptr<TensorOperation> TensorOpCreate::createNew()
{
 return std::unique_ptr<TensorOperation>(new TensorOpCreate());
}

void TensorOpCreate::resetTensorElementType(TensorElementType element_type)
{
 element_type_ = element_type;
 return;
}

void TensorOpCreate::printIt() const
{
 std::cout << "TensorOperation(opcode=" << static_cast<int>(opcode_) << ")[id=" << id_ << "]{" << std::endl;
 if(pattern_.length() > 0) std::cout << " " << pattern_ << std::endl;
 for(const auto & operand: operands_){
  const auto & tensor = std::get<0>(operand);
  if(tensor != nullptr){
   std::cout << " ";
   tensor->printIt();
   std::cout << std::endl;
  }else{
   std::cout << "#ERROR(exatn::TensorOpCreate::printIt): Tensor operand is NULL!" << std::endl << std::flush;
   assert(false);
  }
 }
 for(const auto & scalar: scalars_){
  std::cout << " " << scalar;
 }
 if(scalars_.size() > 0) std::cout << std::endl;
 std::cout << " TensorElementType = " << static_cast<int>(element_type_) << std::endl;
 std::cout << "}" << std::endl;
 return;
}

void TensorOpCreate::printItFile(std::ofstream & output_file) const
{
 output_file << "TensorOperation(opcode=" << static_cast<int>(opcode_) << ")[id=" << id_ << "]{" << std::endl;
 if(pattern_.length() > 0) output_file << " " << pattern_ << std::endl;
 for(const auto & operand: operands_){
  const auto & tensor = std::get<0>(operand);
  if(tensor != nullptr){
   output_file << " ";
   tensor->printItFile(output_file);
   output_file << std::endl;
  }else{
   std::cout << "#ERROR(exatn::TensorOpCreate::printItFile): Tensor operand is NULL!" << std::endl << std::flush;
   assert(false);
  }
 }
 for(const auto & scalar: scalars_){
  output_file << " " << scalar;
 }
 if(scalars_.size() > 0) output_file << std::endl;
 output_file << " TensorElementType = " << static_cast<int>(element_type_) << std::endl;
 output_file << "}" << std::endl;
 //output_file.flush();
 return;
}

std::size_t TensorOpCreate::decompose(const TensorMapper & tensor_mapper)
{
 assert(false);
 //`Implement
 return 0;
}

} //namespace numerics

} //namespace exatn
