/** ExaTN: MPI Communicator Proxy & Process group
REVISION: 2021/07/13

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

#include "mpi_proxy.hpp"

#ifdef MPI_ENABLED
#include "mpi.h"
#endif

#include <cstdlib>

#include <iostream>

namespace exatn {

MPICommProxy::~MPICommProxy()
{
#ifdef MPI_ENABLED
 if(destroy_on_free_){
  if(!(this->isEmpty())){
   if(mpi_comm_ptr_.use_count() == 1){
    auto * mpicomm = this->get<MPI_Comm>();
    int res;
    auto errc = MPI_Comm_compare(*mpicomm,MPI_COMM_WORLD,&res); assert(errc == MPI_SUCCESS);
    if(res != MPI_IDENT){
     errc = MPI_Comm_compare(*mpicomm,MPI_COMM_SELF,&res); assert(errc == MPI_SUCCESS);
     if(res != MPI_IDENT){
      errc = MPI_Comm_free(mpicomm); assert(errc == MPI_SUCCESS);
     }
    }
   }
  }
 }
#endif
}


bool MPICommProxy::operator==(const MPICommProxy & another) const
{
 bool equal = true;
#ifdef MPI_ENABLED
 auto * lhs_comm = this->get<MPI_Comm>();
 auto * rhs_comm = another.get<MPI_Comm>();
 int res;
 auto errc = MPI_Comm_compare(*lhs_comm,*rhs_comm,&res); assert(errc == MPI_SUCCESS);
 equal = (res == MPI_IDENT);
#endif
 return equal;
}


std::shared_ptr<ProcessGroup> ProcessGroup::split(int my_subgroup) const
{
 std::shared_ptr<ProcessGroup> subgroup(nullptr);
 if(this->getSize() == 1){
  if(my_subgroup >= 0) subgroup = std::make_shared<ProcessGroup>(*this);
 }else{
#ifdef MPI_ENABLED
  if(!(intra_comm_.isEmpty())){
   auto & mpicomm = intra_comm_.getRef<MPI_Comm>();
   int color = MPI_UNDEFINED;
   if(my_subgroup >= 0) color = my_subgroup;
   int my_orig_rank;
   auto errc = MPI_Comm_rank(mpicomm,&my_orig_rank); assert(errc == MPI_SUCCESS);
   MPI_Comm subgroup_mpicomm;
   errc = MPI_Comm_split(mpicomm,color,my_orig_rank,&subgroup_mpicomm); assert(errc == MPI_SUCCESS);
   if(color != MPI_UNDEFINED){
    int subgroup_size;
    errc = MPI_Comm_size(subgroup_mpicomm,&subgroup_size); assert(errc == MPI_SUCCESS);
    MPI_Group orig_group,new_group;
    errc = MPI_Comm_group(mpicomm,&orig_group); assert(errc == MPI_SUCCESS);
    errc = MPI_Comm_group(subgroup_mpicomm,&new_group); assert(errc == MPI_SUCCESS);
    int sub_ranks[subgroup_size],orig_ranks[subgroup_size];
    for(int i = 0; i < subgroup_size; ++i) sub_ranks[i] = i;
    errc = MPI_Group_translate_ranks(new_group,subgroup_size,sub_ranks,orig_group,orig_ranks);
    std::vector<unsigned int> subgroup_ranks(subgroup_size); //vector of global MPI ranks
    const auto & ranks = this->getProcessRanks();
    for(int i = 0; i < subgroup_size; ++i) subgroup_ranks[i] = ranks[orig_ranks[i]];
    subgroup = std::make_shared<ProcessGroup>(MPICommProxy(subgroup_mpicomm,true),
                                              subgroup_ranks,
                                              this->getMemoryLimitPerProcess());
   }
  }else{
   std::cout << "#ERROR(exatn::ProcessGroup::split): Empty MPI communicator!\n" << std::flush;
   assert(false);
  }
#endif
 }
 return subgroup;
}

} //namespace exatn
