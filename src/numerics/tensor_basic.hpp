/** ExaTN: Tensor basic types and parameters
REVISION: 2022/01/07

Copyright (C) 2018-2022 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2022 Oak Ridge National Laboratory (UT-Battelle) **/

#ifndef EXATN_NUMERICS_TENSOR_BASIC_HPP_
#define EXATN_NUMERICS_TENSOR_BASIC_HPP_

#include <complex>

#include <cstdint>

namespace exatn{

using Int4 = int32_t;
using Int8 = int64_t;
using UInt4 = uint32_t;
using UInt8 = uint64_t;

using SpaceId = unsigned int;              //space id type
using SubspaceId = unsigned long long int; //subspace id type
using SymmetryId = int;                    //symmetry id type
using DimExtent = unsigned long long int;  //dimension extent type
using DimOffset = unsigned long long int;  //dimension base offset type

using ScopeId = unsigned int; //TAProL scope ID type

constexpr DimExtent MAX_SPACE_DIM = 0xFFFFFFFFFFFFFFFF; //max dimension of unregistered (anonymous) spaces
constexpr SpaceId SOME_SPACE = 0; //any unregistered (anonymous) space (all registered spaces will have SpaceId > 0)
constexpr SubspaceId FULL_SUBSPACE = 0; //every space has its trivial (full) subspace automatically registered as subspace 0
constexpr SubspaceId UNREG_SUBSPACE = 0xFFFFFFFFFFFFFFFF; //id of any unregistered subspace

//Possible types of tensor elements:
enum class TensorElementType{
 VOID,
 REAL16,
 REAL32,
 REAL64,
 COMPLEX16,
 COMPLEX32,
 COMPLEX64
};

//Direction of a leg (directed edge) in a tensor network:
enum class LegDirection{
 UNDIRECT, //no direction
 INWARD,   //inward direction
 OUTWARD   //outward direction
};

//Index kind in a binary tensor contraction (D=L*R):
enum class IndexKind{
 VOID,   //unspecified index kind
 LEFT,   //left open index (shared solely by the destination and left tensor operand)
 RIGHT,  //right open index (shared solely by the destination and right tensor operand)
 CONTR,  //contracted index (shared solely by the left and right tensor operand)
 HYPER,  //hyper index (shared by all three tensor operands: destination, left and right)
 DTRACE, //open index present solely in the destination tensor operand (destination batch)
 LTRACE, //traced index in the left tensor operand
 RTRACE  //traced index in the right tensor operand
};

//Basic tensor operations:
enum class TensorOpCode{
 NOOP,              //0: no operation
 CREATE,            //1: tensor creation
 DESTROY,           //2: tensor destruction
 TRANSFORM,         //3: tensor transformation/initialization
 SLICE,             //4: tensor slicing
 INSERT,            //5: tensor insertion
 ADD,               //6: tensor addition
 CONTRACT,          //7: tensor contraction
 DECOMPOSE_SVD3,    //8: tensor decomposition via SVD into three tensor factors
 DECOMPOSE_SVD2,    //9: tensor decomposition via SVD into two tensor factors
 ORTHOGONALIZE_SVD, //10: tensor orthogonalization via SVD
 ORTHOGONALIZE_MGS, //11: tensor orthogonalization via Modified Gram-Schmidt
 FETCH,             //12: fetch tensor data from another MPI process (parallel execution only)
 UPLOAD,            //13: upload tensor data to another MPI process (parallel execution only)
 BROADCAST,         //14: tensor broadcast (parallel execution only)
 ALLREDUCE          //15: tensor allreduce (parallel execution only)
};


//TensorElementTypeSize<enum TensorElementType>() --> Size in bytes:
template <TensorElementType> constexpr std::size_t TensorElementTypeSize();
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::VOID>(){return 0;} //0 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::REAL16>(){return 2;} //2 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::REAL32>(){return 4;} //4 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::REAL64>(){return 8;} //8 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::COMPLEX16>(){return 4;} //4 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::COMPLEX32>(){return 8;} //8 bytes
template <> constexpr std::size_t TensorElementTypeSize<TensorElementType::COMPLEX64>(){return 16;} //16 bytes

inline std::size_t TensorElementTypeSize(TensorElementType element_type)
{
 switch(element_type){
  case TensorElementType::REAL16: return 2;
  case TensorElementType::REAL32: return 4;
  case TensorElementType::REAL64: return 8;
  case TensorElementType::COMPLEX16: return 4;
  case TensorElementType::COMPLEX32: return 8;
  case TensorElementType::COMPLEX64: return 16;
 }
 return 0;
}

//TensorElementTypeOpFactor<enum TensorElementType>() --> Multiplication factor:
template <TensorElementType> constexpr double TensorElementTypeOpFactor();
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::VOID>(){return 0.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::REAL16>(){return 2.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::REAL32>(){return 2.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::REAL64>(){return 2.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::COMPLEX16>(){return 8.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::COMPLEX32>(){return 8.0;}
template <> constexpr double TensorElementTypeOpFactor<TensorElementType::COMPLEX64>(){return 8.0;}

inline double tensorElementTypeOpFactor(const TensorElementType element_type){
 switch(element_type){
  case TensorElementType::REAL16: return 2.0;
  case TensorElementType::REAL32: return 2.0;
  case TensorElementType::REAL64: return 2.0;
  case TensorElementType::COMPLEX16: return 8.0;
  case TensorElementType::COMPLEX32: return 8.0;
  case TensorElementType::COMPLEX64: return 8.0;
 }
 return 0.0;
}

//TensorDataType<enum TensorElementType>::value --> C++ type:
template <TensorElementType> struct TensorDataType{
 using value = void;
};
template <> struct TensorDataType<TensorElementType::REAL32>{
 using value = float;
 static constexpr const value ZERO {0.0f};
 static constexpr const value UNITY {1.0f};
 static constexpr std::size_t size() {return sizeof(value);}
};
template <> struct TensorDataType<TensorElementType::REAL64>{
 using value = double;
 static constexpr const value ZERO {0.0};
 static constexpr const value UNITY {1.0};
 static constexpr std::size_t size() {return sizeof(value);}
};
template <> struct TensorDataType<TensorElementType::COMPLEX32>{
 using value = std::complex<float>;
 static constexpr const value ZERO {0.0f,0.0f};
 static constexpr const value UNITY {1.0f,0.0f};
 static constexpr std::size_t size() {return sizeof(value);}
};
template <> struct TensorDataType<TensorElementType::COMPLEX64>{
 using value = std::complex<double>;
 static constexpr const value ZERO {0.0,0.0};
 static constexpr const value UNITY {1.0,0.0};
 static constexpr std::size_t size() {return sizeof(value);}
};

//TensorDataKind<C++ type>::value --> enum TensorElementType:
template <typename T> struct TensorDataKind{
 static constexpr const TensorElementType value = TensorElementType::VOID;
};
template <> struct TensorDataKind<float>{
 static constexpr const TensorElementType value = TensorElementType::REAL32;
 static constexpr const float ZERO {0.0f};
 static constexpr const float UNITY {1.0f};
 static constexpr std::size_t size() {return sizeof(float);}
};
template <> struct TensorDataKind<double>{
 static constexpr const TensorElementType value = TensorElementType::REAL64;
 static constexpr const double ZERO {0.0};
 static constexpr const double UNITY {1.0};
 static constexpr std::size_t size() {return sizeof(double);}
};
template <> struct TensorDataKind<std::complex<float>>{
 static constexpr const TensorElementType value = TensorElementType::COMPLEX32;
 static constexpr const std::complex<float> ZERO {0.0f,0.0f};
 static constexpr const std::complex<float> UNITY {1.0f,0.0f};
 static constexpr std::size_t size() {return sizeof(std::complex<std::complex<float>>);}
};
template <> struct TensorDataKind<std::complex<double>>{
 static constexpr const TensorElementType value = TensorElementType::COMPLEX64;
 static constexpr const std::complex<double> ZERO {0.0,0.0};
 static constexpr const std::complex<double> UNITY {1.0,0.0};
 static constexpr std::size_t size() {return sizeof(std::complex<std::complex<double>>);}
};

} //namespace exatn

#endif //EXATN_NUMERICS_TENSOR_BASIC_HPP_
