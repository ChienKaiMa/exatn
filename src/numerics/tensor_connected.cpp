/** ExaTN::Numerics: Tensor connected to other tensors inside a tensor network
REVISION: 2021/04/27

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

#include "tensor_connected.hpp"
#include "tensor_symbol.hpp"

#include <algorithm>

namespace exatn{

namespace numerics{

TensorConn::TensorConn(std::shared_ptr<Tensor> tensor,
                       unsigned int id,
                       const std::vector<TensorLeg> & legs,
                       bool conjugated):
 tensor_(tensor), id_(id), legs_(legs), conjugated_(conjugated), optimizable_(false)
{
}

void TensorConn::printIt(bool with_hash) const
{
 std::cout << id_ << ": ";
 tensor_->printIt(with_hash);
 if(conjugated_) std::cout << "+";
 std::cout << ": { ";
 for(const auto & leg: legs_) leg.printIt();
 std::cout << " }" << std::endl;
 return;
}

void TensorConn::printItFile(std::ofstream & output_file,
                             bool with_hash) const
{
 output_file << id_ << ": ";
 tensor_->printItFile(output_file,with_hash);
 if(conjugated_) output_file << "+";
 output_file << ": { ";
 for(const auto & leg: legs_) leg.printItFile(output_file);
 output_file << " }" << std::endl;
 return;
}

const std::string & TensorConn::getName() const
{
 assert(tensor_);
 return tensor_->getName();
}

const TensorShape & TensorConn::getShape() const
{
 assert(tensor_);
 return tensor_->getShape();
}

const TensorSignature & TensorConn::getSignature() const
{
 assert(tensor_);
 return tensor_->getSignature();
}

unsigned int TensorConn::getNumLegs() const
{
 return tensor_->getRank();
}

unsigned int TensorConn::getRank() const
{
 return tensor_->getRank();
}

bool TensorConn::isComplexConjugated() const
{
 return conjugated_;
}

std::shared_ptr<Tensor> TensorConn::getTensor() const
{
 return tensor_;
}

unsigned int TensorConn::getTensorId() const
{
 return id_;
}

void TensorConn::resetTensorId(unsigned int tensor_id)
{
 id_ = tensor_id;
}

const TensorLeg & TensorConn::getTensorLeg(unsigned int leg_id) const
{
 assert(leg_id < legs_.size());
 return legs_[leg_id];
}

const std::vector<TensorLeg> & TensorConn::getTensorLegs() const
{
 return legs_;
}

const std::vector<DimExtent> & TensorConn::getDimExtents() const
{
 return tensor_->getDimExtents();
}

DimExtent TensorConn::getDimExtent(unsigned int dim_id) const
{
 return tensor_->getDimExtent(dim_id);
}

std::pair<SpaceId,SubspaceId> TensorConn::getDimSpaceAttr(unsigned int dim_id) const
{
 return tensor_->getDimSpaceAttr(dim_id);
}

void TensorConn::resetLeg(unsigned int leg_id, TensorLeg tensor_leg)
{
 assert(leg_id < legs_.size());
 legs_[leg_id].resetConnection(tensor_leg.getTensorId(),
                               tensor_leg.getDimensionId(),
                               tensor_leg.getDirection());
 return;
}

void TensorConn::deleteLeg(unsigned int leg_id)
{
 assert(leg_id < legs_.size());
 legs_.erase(legs_.cbegin()+leg_id);
 tensor_->deleteDimension(leg_id);
 return;
}

void TensorConn::deleteLegs(std::vector<unsigned int> & leg_ids)
{
 if(leg_ids.size() > 0){
  std::sort(leg_ids.begin(),leg_ids.end());
  unsigned int deleted = 0;
  for(const auto & leg_id: leg_ids){
   this->deleteLeg(leg_id-deleted);
   ++deleted;
  }
 }
 return;
}

void TensorConn::appendLeg(std::pair<SpaceId,SubspaceId> subspace, DimExtent dim_extent, TensorLeg tensor_leg)
{
 tensor_->appendDimension(subspace,dim_extent);
 legs_.emplace_back(tensor_leg);
 return;
}

void TensorConn::appendLeg(DimExtent dim_extent, TensorLeg tensor_leg)
{
 this->appendLeg(std::pair<SpaceId,SubspaceId>{SOME_SPACE,0},dim_extent,tensor_leg);
 return;
}

void TensorConn::conjugate()
{
 if(id_ != 0) conjugated_ = !conjugated_; //output tensors do not conjugate
 for(auto & leg: legs_) leg.reverseDirection();
 return;
}

void TensorConn::conjugate(bool conjug)
{
 if(conjugated_ != conjug) return conjugate();
 return;
}

const Metadata & TensorConn::getMetadata() const
{
 return metadata_;
}

void TensorConn::attachMetadata(const Metadata & metadata)
{
 metadata_ = metadata;
 return;
}

void TensorConn::replaceStoredTensor(const std::string & name)
{
 assert(tensor_);
 const auto & old_tensor = *tensor_;
 tensor_ = makeSharedTensor(old_tensor);
 auto new_name(name);
 if(new_name.empty()) new_name = tensor_hex_name("z",tensor_->getTensorHash());
 tensor_->rename(new_name);
 return;
}

void TensorConn::replaceStoredTensor(const std::vector<unsigned int> & order,
                                     const std::string & name)
{
 assert(tensor_);
 const auto rank = tensor_->getRank();
 assert(rank == order.size());
 const auto & old_tensor = *tensor_;
 tensor_ = makeSharedTensor(old_tensor,order);
 if(rank > 0){
  TensorLeg old_legs[rank];
  for(unsigned int i = 0; i < rank; ++i) old_legs[i] = legs_[i];
  for(unsigned int i = 0; i < rank; ++i) legs_[i] = old_legs[order[i]];
 }
 auto new_name(name);
 if(new_name.empty()) new_name = tensor_hex_name("z",tensor_->getTensorHash());
 tensor_->rename(new_name);
 return;
}

void TensorConn::replaceStoredTensor(std::shared_ptr<Tensor> tensor)
{
 assert(tensor);
 tensor_ = tensor;
 return;
}

const std::list<std::vector<unsigned int>> & TensorConn::retrieveIsometries() const
{
 return tensor_->retrieveIsometries();
}

bool TensorConn::withIsometricDimension(unsigned int dim_id, const std::vector<unsigned int> ** iso_group) const
{
 return tensor_->withIsometricDimension(dim_id,iso_group);
}

bool TensorConn::isOptimizable() const
{
 return optimizable_;
}

void TensorConn::resetOptimizability(bool optimizable)
{
 assert(optimizable == false || id_ != 0); //output tensor cannot be optimizable
 optimizable_ = optimizable;
 return;
}

TensorElementType TensorConn::getElementType() const
{
 assert(tensor_);
 return tensor_->getElementType();
}

} //namespace numerics

} //namespace exatn
