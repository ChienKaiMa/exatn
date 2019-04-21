/** ExaTN::Numerics: Basis Vector
REVISION: 2019/04/20

Copyright (C) 2018-2019 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2019 Oak Ridge National Laboratory (UT-Battelle) **/

/** Rationale:
 (a) Any basis vector is a 1-dimensional subspace;
 (b) Any space/subspace can be composed of linear-independent
     1-dimensional subspaces by taking a direct sum of them.
 (c) Any abstract basis vector can be further specialized by
     introducing additional attributes peculiar to a specific basis kind.
**/

#ifndef BASIS_VECTOR_HPP_
#define BASIS_VECTOR_HPP_

#include "tensor_basic.hpp"

namespace exatn{

namespace numerics{

class BasisVector{
public:

 BasisVector(SubspaceId id = UNREG_SUBSPACE);

 BasisVector(const BasisVector & basis_vector) = default;
 BasisVector & operator=(const BasisVector & basis_vector) = default;
 BasisVector(BasisVector && basis_vector) = default;
 BasisVector & operator=(BasisVector && basis_vector) = default;
 virtual ~BasisVector() = default;

 /** Print. **/
 void printIt() const;

private:

 SubspaceId id_; //basis vector id (>=0)

};

} //namespace numerics

} //namespace exatn

#endif //BASIS_VECTOR_HPP_
