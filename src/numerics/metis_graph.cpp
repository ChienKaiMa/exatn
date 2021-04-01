/** ExaTN::Numerics: Graph k-way partitioning via METIS
REVISION: 2021/04/01

Copyright (C) 2018-2021 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2021 Oak Ridge National Laboratory (UT-Battelle) **/

#include "metis_graph.hpp"

#include "tensor_network.hpp"

#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <tuple>

#include <cmath>

namespace exatn{

namespace numerics{

void MetisGraph::initMetisGraph() //protected helper
{
 METIS_SetDefaultOptions(options_);
 options_[METIS_OPTION_PTYPE] = METIS_PTYPE_KWAY;
 options_[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
 options_[METIS_OPTION_NUMBERING] = 0;
 options_[METIS_OPTION_MINCONN] = 1;
 options_[METIS_OPTION_CONTIG] = 0;
 options_[METIS_OPTION_CCORDER] = 1;

 num_vertices_ = 0;
 num_parts_ = 0;
 edge_cut_ = 0;
 num_cross_edges_ = 0;
 xadj_.emplace_back(0);
 return;
}


MetisGraph::MetisGraph():
 num_vertices_(0), num_parts_(0), edge_cut_(0), num_cross_edges_(0)
{
 initMetisGraph();
}


MetisGraph::MetisGraph(const TensorNetwork & network):
 MetisGraph()
{
 //Map tensor Ids to a consecutive integer range [0..N-1], N is the number of input tensors:
 std::unordered_map<unsigned int, unsigned int> tensor_id_map; //tensor_id --> vertex_id [0..N-1]
 unsigned int vertex_id = 0;
 for(auto iter = network.cbegin(); iter != network.cend(); ++iter){
  if(iter->first != 0){ //ignore the output tensor
   renumber_.emplace_back(iter->first); //new vertex id --> old tensor id
   auto res = tensor_id_map.emplace(std::make_pair(iter->first,vertex_id++)); //old tensor id --> new vertex id
   assert(res.second);
  }
 }
 //Generate the adjacency list:
 auto cmp = [](std::pair<unsigned int, DimExtent> & a,
               std::pair<unsigned int, DimExtent> & b){return (a.first < b.first);};
 for(auto iter = network.cbegin(); iter != network.cend(); ++iter){
  if(iter->first != 0){ //ignore the output tensor (it is not a real vertex of the graph)
   const auto tensor_rank = iter->second.getRank();
   const auto & tensor_dims = iter->second.getDimExtents();
   const auto & tensor_legs = iter->second.getTensorLegs();
   std::vector<std::pair<unsigned int, DimExtent>> edges(tensor_rank); //vertex edges
   for(unsigned int i = 0; i < tensor_rank; ++i){
    edges[i] = std::pair<unsigned int, DimExtent>{tensor_legs[i].getTensorId(), //connected tensor id
                                                  tensor_dims[i]}; //edge dimension
   }
   //Remap tensor id to the vertex id:
   std::sort(edges.begin(),edges.end(),cmp); //order adjacent tensors by their Ids
   std::size_t adj_vertices[tensor_rank], edge_weights[tensor_rank];
   std::size_t vertex_weight = 1; //default vertex weight (if no open edges)
   unsigned int first_vertex_pos = tensor_rank;
   for(unsigned int i = 0; i < tensor_rank; ++i){
    if(edges[i].first == 0){ //connections to the output tensor are not counted as edges
     vertex_weight *= edges[i].second; //they are absorbed into the vertex weight
    }else{
     if(first_vertex_pos == tensor_rank) first_vertex_pos = i; //position of the first real edge
     edges[i].first = tensor_id_map[edges[i].first]; //adjacent tensor id --> adjacent vertex id
    }
   }
   //Compute edge weights and adjacency list:
   std::size_t num_edges = 0;
   std::size_t edge_weight = 1;
   int current_vertex = -1;
   for(int i = first_vertex_pos; i < tensor_rank; ++i){ //consider only real edges
    if(edges[i].first != current_vertex){
     if(current_vertex >= 0){ //record previous edge
      adj_vertices[num_edges] = current_vertex;
      //edge_weights[num_edges++] = edge_weight;
      edge_weights[num_edges++] = static_cast<std::size_t>(std::log2(static_cast<double>(edge_weight))) + 1; //weight is always shifted by one
     }
     current_vertex = edges[i].first;
     edge_weight = edges[i].second;
    }else{
     edge_weight *= edges[i].second;
    }
   }
   if(current_vertex >= 0){ //record last edge
    adj_vertices[num_edges] = current_vertex;
    //edge_weights[num_edges++] = edge_weight;
    edge_weights[num_edges++] = static_cast<std::size_t>(std::log2(static_cast<double>(edge_weight))) + 1; //weight is always shifted by one
   }
   vertex_weight = static_cast<std::size_t>(std::log2(static_cast<double>(vertex_weight))) + 1; //weight is always shifted by one
   appendVertex(num_edges,adj_vertices,edge_weights,vertex_weight);
  }
 }
}


MetisGraph::MetisGraph(const MetisGraph & parent, //in: partitioned parental graph
                       std::size_t partition):    //in: partition of the parental graph
 MetisGraph()
{
 if(partition < parent.num_parts_){
  num_vertices_ = 0;
  //Collect the vertices and edges from the requested partition:
  std::unordered_map<idx_t,idx_t> vertex_id_map; //parent vertex id --> child vertex id
  for(idx_t vert = 0; vert < parent.partitions_.size(); ++vert){
   if(parent.partitions_[vert] == partition){ //vertex belongs to the requested partition
    auto res = vertex_id_map.emplace(std::make_pair(vert,num_vertices_)); assert(res.second);
    vwgt_.emplace_back(parent.vwgt_[vert]);
    idx_t num_edges = 0;
    for(idx_t edge = parent.xadj_[vert]; edge < parent.xadj_[vert+1]; ++edge){
     const auto & adj_vertex_id = parent.adjncy_[edge];
     if(parent.partitions_[adj_vertex_id] == partition){ //internal edge: Copy it
      adjncy_.emplace_back(adj_vertex_id);
      adjwgt_.emplace_back(parent.adjwgt_[edge]);
      ++num_edges;
     }else{ //external edge: Aggregate into the vertex weight
      vwgt_[num_vertices_] += (parent.adjwgt_[edge] - 1);
     }
    }
    xadj_.emplace_back(xadj_[num_vertices_] + num_edges);
    ++num_vertices_;
   }
  }
  //Renumber vertex Ids:
  for(auto & adj_vertex_id: adjncy_) adj_vertex_id = vertex_id_map[adj_vertex_id];
  //Create updated renumbering:
  for(idx_t vert = 0; vert < parent.renumber_.size(); ++vert){
   if(parent.partitions_[vert] == partition) renumber_.emplace_back(parent.renumber_[vert]);
  }
 }else{
  std::cout << "#ERROR(exatn::numerics::MetisGraph): Partition does not exist in the parent graph!\n";
  assert(false);
 }
}


MetisGraph::MetisGraph(const MetisGraph & parent,                    //in: partitioned parental graph
                       const std::vector<std::size_t> & partitions): //in: partitions of the parental graph
 MetisGraph()
{
 auto is_contained = [&partitions](const std::size_t partn){
                      return (std::find(partitions.cbegin(),partitions.cend(),partn) != partitions.cend());
                     };
 num_vertices_ = 0;
 //Collect the vertices and edges from the requested partition:
 std::unordered_map<idx_t,idx_t> vertex_id_map; //parent vertex id --> child vertex id
 for(idx_t vert = 0; vert < parent.partitions_.size(); ++vert){
  if(is_contained(parent.partitions_[vert])){ //vertex belongs to the requested partitions
   auto res = vertex_id_map.emplace(std::make_pair(vert,num_vertices_)); assert(res.second);
   vwgt_.emplace_back(parent.vwgt_[vert]);
   idx_t num_edges = 0;
   for(idx_t edge = parent.xadj_[vert]; edge < parent.xadj_[vert+1]; ++edge){
    const auto & adj_vertex_id = parent.adjncy_[edge];
    if(is_contained(parent.partitions_[adj_vertex_id])){ //internal edge: Copy it
     adjncy_.emplace_back(adj_vertex_id);
     adjwgt_.emplace_back(parent.adjwgt_[edge]);
     ++num_edges;
    }else{ //external edge: Aggregate into the vertex weight
     vwgt_[num_vertices_] += (parent.adjwgt_[edge] - 1);
    }
   }
   xadj_.emplace_back(xadj_[num_vertices_] + num_edges);
   ++num_vertices_;
  }
 }
 //Renumber vertex Ids:
 for(auto & adj_vertex_id: adjncy_) adj_vertex_id = vertex_id_map[adj_vertex_id];
 //Create updated renumbering:
 for(idx_t vert = 0; vert < parent.renumber_.size(); ++vert){
  if(is_contained(parent.partitions_[vert])) renumber_.emplace_back(parent.renumber_[vert]);
 }
}


MetisGraph::MetisGraph(BytePacket & byte_packet):
 MetisGraph()
{
 unpack(byte_packet);
}


void MetisGraph::pack(BytePacket & byte_packet) const
{
 //num_vertices_:
 appendToBytePacket(&byte_packet,num_vertices_);
 //renumber_:
 std::size_t length = renumber_.size();
 appendToBytePacket(&byte_packet,length);
 for(std::size_t i = 0; i < length; ++i) appendToBytePacket(&byte_packet,renumber_[i]);
 //xadj_:
 length = xadj_.size();
 appendToBytePacket(&byte_packet,length);
 for(std::size_t i = 0; i < length; ++i) appendToBytePacket(&byte_packet,xadj_[i]);
 //adjncy_:
 length = adjncy_.size();
 appendToBytePacket(&byte_packet,length);
 for(std::size_t i = 0; i < length; ++i) appendToBytePacket(&byte_packet,adjncy_[i]);
 //vwgt_:
 length = vwgt_.size();
 appendToBytePacket(&byte_packet,length);
 for(std::size_t i = 0; i < length; ++i) appendToBytePacket(&byte_packet,vwgt_[i]);
 //adjwgt_:
 length = adjwgt_.size();
 appendToBytePacket(&byte_packet,length);
 for(std::size_t i = 0; i < length; ++i) appendToBytePacket(&byte_packet,adjwgt_[i]);
 return;
}


void MetisGraph::unpack(BytePacket & byte_packet)
{
 //num_vertices_:
 extractFromBytePacket(&byte_packet,num_vertices_);
 //renumber_:
 std::size_t length = 0;
 extractFromBytePacket(&byte_packet,length);
 renumber_.resize(length);
 for(std::size_t i = 0; i < length; ++i) extractFromBytePacket(&byte_packet,renumber_[i]);
 //xadj_:
 extractFromBytePacket(&byte_packet,length);
 xadj_.resize(length);
 for(std::size_t i = 0; i < length; ++i) extractFromBytePacket(&byte_packet,xadj_[i]);
 //adjncy_:
 extractFromBytePacket(&byte_packet,length);
 adjncy_.resize(length);
 for(std::size_t i = 0; i < length; ++i) extractFromBytePacket(&byte_packet,adjncy_[i]);
 //vwgt_:
 extractFromBytePacket(&byte_packet,length);
 vwgt_.resize(length);
 for(std::size_t i = 0; i < length; ++i) extractFromBytePacket(&byte_packet,vwgt_[i]);
 //adjwgt_:
 extractFromBytePacket(&byte_packet,length);
 adjwgt_.resize(length);
 for(std::size_t i = 0; i < length; ++i) extractFromBytePacket(&byte_packet,adjwgt_[i]);
 return;
}


bool operator==(const MetisGraph & lhs, const MetisGraph & rhs)
{
 return std::tie(lhs.num_vertices_,lhs.xadj_,lhs.adjncy_,lhs.vwgt_,lhs.adjwgt_,lhs.renumber_)
     == std::tie(rhs.num_vertices_,rhs.xadj_,rhs.adjncy_,rhs.vwgt_,rhs.adjwgt_,rhs.renumber_);
}


bool operator!=(const MetisGraph & lhs, const MetisGraph & rhs)
{
 return !(lhs == rhs);
}


void MetisGraph::clearPartitions()
{
 tpwgts_.clear();
 ubvec_.clear();
 partitions_.clear();
 part_weights_.clear();
 edge_cut_ = 0;
 num_cross_edges_ = 0;
 num_parts_ = 0;
 return;
}


void MetisGraph::clear()
{
 clearPartitions();
 renumber_.clear();
 xadj_.clear();
 adjncy_.clear();
 vwgt_.clear();
 adjwgt_.clear();
 num_vertices_ = 0;
 initMetisGraph();
 return;
}


std::size_t MetisGraph::getNumVertices() const
{
 return static_cast<std::size_t>(num_vertices_);
}


std::size_t MetisGraph::getNumPartitions() const
{
 return static_cast<std::size_t>(num_parts_);
}


void MetisGraph::appendVertex(std::size_t num_edges,      //in: number of edges (number of adjacent vertices)
                              std::size_t * adj_vertices, //in: adjacent vertices (numbering starts from 0)
                              std::size_t * edge_weights, //in: edge weights (>0)
                              std::size_t vertex_weight)  //in: vertex weight (>=0)
{
 if(num_parts_ > 0) clearPartitions();
 for(std::size_t i = 0; i < num_edges; ++i) adjncy_.emplace_back(adj_vertices[i]);
 for(std::size_t i = 0; i < num_edges; ++i) adjwgt_.emplace_back(edge_weights[i]);
 xadj_.emplace_back(xadj_[num_vertices_] + num_edges);
 vwgt_.emplace_back(vertex_weight);
 ++num_vertices_;
 return;
}


double MetisGraph::getContractionCost(std::size_t vertex1, //in: first vertex id [0..N-1]
                                      std::size_t vertex2, //in: second vertex id [0..N-1]
                                      double * intermediate_volume, //out: volume of the produced intermediate
                                      double * diff_volume) const   //out: differential volume (intermediate volume - input volumes)
{
 double flops = 0.0; //FMA flop estimate
 if(vertex1 != vertex2 && vertex1 < num_vertices_ && vertex2 < num_vertices_){
  if(vertex1 > vertex2) std::swap(vertex1,vertex2);
  double left_vol = std::pow(2.0,static_cast<double>(vwgt_[vertex1]-1));  //open volume of vertex1
  double right_vol = std::pow(2.0,static_cast<double>(vwgt_[vertex2]-1)); //open volume of vertex2
  double contr_vol = 1.0;
  for(auto i = xadj_[vertex1]; i < xadj_[vertex1+1]; ++i){ //edges of vertex1
   double vol = std::pow(2.0,static_cast<double>(adjwgt_[i]-1)); //edge volume
   if(adjncy_[i] == vertex2) contr_vol *= vol;
   left_vol *= vol;
  }
  for(auto i = xadj_[vertex2]; i < xadj_[vertex2+1]; ++i){ //edges of vertex2
   double vol = std::pow(2.0,static_cast<double>(adjwgt_[i]-1)); //edge volume
   right_vol *= vol;
  }
  double inter_vol = left_vol * right_vol / (contr_vol * contr_vol);
  if(intermediate_volume != nullptr) *intermediate_volume = inter_vol;
  if(diff_volume != nullptr) *diff_volume = inter_vol - (left_vol + right_vol);
 }
 return flops; //FMA flop estimate
}


bool MetisGraph::mergeVertices(std::size_t vertex1, //in: first vertex id [0..N-1]
                               std::size_t vertex2) //in: second vertex id [0..N-1]
{
 if(num_parts_ > 0) clearPartitions();
 if(!renumber_.empty()) renumber_.clear();
 assert(xadj_.size() == (num_vertices_ + 1) && vwgt_.size() == num_vertices_);
 if(vertex1 == vertex2 || vertex1 >= num_vertices_ || vertex2 >= num_vertices_) return false;
 if(vertex1 > vertex2) std::swap(vertex1,vertex2);
 //Absorb the weight of vertex 2 into vertex 1:
 vwgt_[vertex1] += (vwgt_[vertex2] - 1); //combine log open volumes into vertex1
 vwgt_.erase(vwgt_.begin()+vertex2); //delete vertex2
 //Move the edges of vertex 2 and append them to vertex 1:
 const idx_t num_edges1 = xadj_[vertex1+1] - xadj_[vertex1]; //number of edges incident to vertex 1
 const idx_t num_edges2 = xadj_[vertex2+1] - xadj_[vertex2]; //number of edges incident to vertex 2
 if(num_edges2 > 0){
  idx_t edges2[num_edges2], edge_weights2[num_edges2];
  idx_t offset = xadj_[vertex2];
  for(idx_t i = 0; i < num_edges2; ++i) edges2[i] = adjncy_[offset + i];
  for(idx_t i = 0; i < num_edges2; ++i) edge_weights2[i] = adjwgt_[offset + i];
  for(idx_t i = xadj_[vertex2] - 1; i >= xadj_[vertex1+1]; --i) adjncy_[i+num_edges2] = adjncy_[i];
  for(idx_t i = xadj_[vertex2] - 1; i >= xadj_[vertex1+1]; --i) adjwgt_[i+num_edges2] = adjwgt_[i];
  offset = xadj_[vertex1+1];
  for(idx_t i = 0; i < num_edges2; ++i) adjncy_[offset+i] = edges2[i];
  for(idx_t i = 0; i < num_edges2; ++i) adjwgt_[offset+i] = edge_weights2[i];
  for(idx_t vert = vertex1 + 1; vert <= vertex2; ++vert) xadj_[vert] += num_edges2;
 }
 //Sort the combined edges within vertex 1:
 idx_t edges1[num_edges1], edge_weights1[num_edges1];
 idx_t edges2[num_edges2], edge_weights2[num_edges2];
 idx_t offset = xadj_[vertex1];
 for(idx_t i = 0; i < num_edges1; ++i){
  edges1[i] = adjncy_[offset];
  edge_weights1[i] = adjncy_[offset++];
 }
 for(idx_t i = 0; i < num_edges2; ++i){
  edges2[i] = adjncy_[offset];
  edge_weights2[i] = adjncy_[offset++];
 }
 offset = xadj_[vertex1];
 idx_t j1 = 0, j2 = 0;
 bool not_done = true;
 while(not_done){
  if(j1 < num_edges1 && j2 < num_edges2){
   if(edges1[j1] <= edges2[j2]){
    adjncy_[offset] = edges1[j1];
    adjwgt_[offset++] = edge_weights1[j1++];
   }else{
    adjncy_[offset] = edges2[j2];
    adjwgt_[offset++] = edge_weights2[j2++];
   }
  }else if(j1 < num_edges1 && j2 == num_edges2){
   adjncy_[offset] = edges1[j1];
   adjwgt_[offset++] = edge_weights1[j1++];
  }else if(j1 == num_edges1 && j2 < num_edges2){
   adjncy_[offset] = edges2[j2];
   adjwgt_[offset++] = edge_weights2[j2++];
  }else{
   not_done = false;
  }
 }
 assert(offset == xadj_[vertex1+1]);
 //Merge the edges incident to the same adjacent vertex and delete redundant edges:
 idx_t num_deleted = 0;
 for(idx_t i = xadj_[vertex1+1] - 1; i >= xadj_[vertex1] + 1; --i){
  if(adjncy_[i] == vertex1 || adjncy_[i] == vertex2){ //contracted edge: Delete
   adjncy_.erase(adjncy_.begin()+i);
   adjwgt_.erase(adjwgt_.begin()+i);
   ++num_deleted;
  }else{
   if(adjncy_[i] == adjncy_[i-1]){ //redundant edge: Absorb and delete
    adjwgt_[i-1] += (adjwgt_[i] - 1);
    adjncy_.erase(adjncy_.begin()+i);
    adjwgt_.erase(adjwgt_.begin()+i);
    ++num_deleted;
   }
  }
 }
 if(adjncy_[xadj_[vertex1]] == vertex1){
  adjncy_.erase(adjncy_.begin()+xadj_[vertex1]);
  adjwgt_.erase(adjwgt_.begin()+xadj_[vertex1]);
  ++num_deleted;
 }
 if(num_deleted > 0){
  for(idx_t vert = vertex1 + 1; vert <= num_vertices_; ++vert) xadj_[vert] -= num_deleted;
 }
 xadj_.erase(xadj_.begin()+vertex2); //delete vertex 2
 //Reassign all edges incident to vertex 2 to vertex 1:
 for(auto & adj_vertex_id: adjncy_){if(adj_vertex_id == vertex2) adj_vertex_id = vertex1;}
 --num_vertices_;
 assert(xadj_.size() == (num_vertices_ + 1) && vwgt_.size() == num_vertices_);
 assert(adjncy_.size() == adjwgt_.size() && adjncy_.size() == xadj_[num_vertices_]);
 return true;
}


bool MetisGraph::partitionGraph(std::size_t num_parts, //in: number of parts (>0)
                                double imbalance)      //in: imbalance tolerance (>= 1.0)
{
 assert(num_vertices_ > 0);
 assert(num_parts > 0);
 assert(imbalance >= 1.0);
 if(num_parts_ > 0) clearPartitions();
 num_parts_ = std::min(static_cast<idx_t>(num_parts),num_vertices_);
 partitions_.resize(num_vertices_);
 idx_t ncon = 1;
 auto errc = METIS_PartGraphKway(&num_vertices_,&ncon,xadj_.data(),adjncy_.data(),
                                 vwgt_.data(),NULL,adjwgt_.data(),&num_parts_,
                                 NULL,&imbalance,options_,&edge_cut_,partitions_.data());
 /* Without edge weights:
 auto errc = METIS_PartGraphKway(&num_vertices_,&ncon,xadj_.data(),adjncy_.data(),
                                 vwgt_.data(),NULL,NULL,&num_parts_,
                                 NULL,&imbalance,options_,&edge_cut_,partitions_.data()); */
 num_cross_edges_ = 0;
 if(errc == METIS_OK){
  //Compute partition weights and the number of cross edges between partitions:
  part_weights_.assign(num_parts_,0);
  for(idx_t vert = 0; vert < num_vertices_; ++vert){
   const auto partition = partitions_[vert];
   part_weights_[partition] += vwgt_[vert];
   for(auto edge = xadj_[vert]; edge < xadj_[vert+1]; ++edge){
    if(partitions_[adjncy_[edge]] != partition) ++num_cross_edges_;
   }
  }
  assert(num_cross_edges_ % 2 == 0);
  num_cross_edges_ /= 2;
 }else{
  std::cout << "#ERROR(exatn::numerics::MetisGraph): METIS_PartGraphKway error " << errc << std::endl;
 }
 return (errc == METIS_OK);
}


bool MetisGraph::partitionGraph(std::size_t num_parts,     //in: number of parts (>0)
                                std::size_t num_miniparts, //in: number of minipartitions prior to merging
                                double imbalance)          //in: imbalance tolerance (>= 1.0)
{
 assert(num_miniparts >= num_parts);
 //Partition the graph into minipartitions:
 bool success = partitionGraph(num_miniparts,imbalance);
 //Merge minipartitions into macropartitions:
 if(success && num_miniparts > num_parts){
  //Compute the coarse adjacency matrix:
  std::size_t adj[num_miniparts][num_miniparts];
  for(int i = 0; i < num_miniparts; ++i){
   for(int j = 0; j < num_miniparts; ++j) adj[i][j] = 0;
  }
  for(idx_t vert = 0; vert < num_vertices_; ++vert){
   const auto partition = partitions_[vert];
   for(auto edge = xadj_[vert]; edge < xadj_[vert+1]; ++edge){
    const auto adj_partition = partitions_[adjncy_[edge]];
    adj[partition][adj_partition] += adjwgt_[edge];
   }
  }
  /*//DEBUG BEGIN:
  std::cout << "#DEBUG(exatn::numerics::MetisGraph::partitionGraph): Coarsened adjacency matrix:\n";
  for(idx_t i = 0; i < num_miniparts; ++i){
   for(idx_t j = 0; j < num_miniparts; ++j){
    std::cout << " " << adj[i][j];
   }
   std::cout << std::endl;
  }
  //DEBUG END*/
  //Construct the coarse graph:
  MetisGraph coarse;
  for(idx_t i = 0; i < num_miniparts; ++i){
   std::size_t adj_verts[num_miniparts], adj_wghts[num_miniparts];
   std::size_t num_adj_edges = 0;
   for(idx_t j = 0; j < num_miniparts; ++j){
    if(j != i && adj[i][j] != 0){
     adj_verts[num_adj_edges] = j;
     adj_wghts[num_adj_edges] = adj[i][j];
     ++num_adj_edges;
    }
   }
   coarse.appendVertex(num_adj_edges,adj_verts,adj_wghts,part_weights_[i]);
  }
  //Bipartition the coarse graph:
  success = coarse.partitionGraph(num_parts,imbalance);
  if(success){
   std::size_t edgecut, numcross;
   const std::vector<idx_t> * part_weights;
   const auto & parts = coarse.getPartitions(&edgecut,&numcross,&part_weights);
   edge_cut_ = edgecut;
   part_weights_ = *part_weights;
   for(auto & partition: partitions_) partition = parts[partition];
   //Recompute the number of cross edges:
   num_cross_edges_ = 0;
   for(idx_t vert = 0; vert < num_vertices_; ++vert){
    const auto partition = partitions_[vert];
    for(auto edge = xadj_[vert]; edge < xadj_[vert+1]; ++edge){
     if(partitions_[adjncy_[edge]] != partition) ++num_cross_edges_;
    }
   }
   assert(num_cross_edges_ % 2 == 0);
   num_cross_edges_ /= 2;
   num_parts_ = num_parts;
  }
 }
 return success;
}


const std::vector<idx_t> & MetisGraph::getPartitions(std::size_t * edge_cut,
                                                     std::size_t * num_cross_edges,
                                                     const std::vector<idx_t> ** part_weights,
                                                     const std::vector<idx_t> ** renumbering) const
{
 if(edge_cut != nullptr) *edge_cut = edge_cut_;
 if(num_cross_edges != nullptr) *num_cross_edges = num_cross_edges_;
 if(part_weights != nullptr) *part_weights = &part_weights_;
 if(renumbering != nullptr){
  if(renumber_.empty()){
   *renumbering = nullptr;
  }else{
   *renumbering = &renumber_;
  }
 }
 return partitions_;
}


std::size_t MetisGraph::getOriginalVertexId(std::size_t vertex_id) const
{
 if(!renumber_.empty()) return static_cast<std::size_t>(renumber_[vertex_id]);
 return vertex_id;
}


void MetisGraph::printAdjacencyMatrix() const
{
 std::cout << "#INFO(exatn::numerics::MetisGraph::printAdjacencyMatrix): Graph adjacency matrix:\n";
 for(idx_t vert = 0; vert < num_vertices_; ++vert){
  std::cout << "Vertex " << vert << " [" << vwgt_[vert] << "]:";
  for(idx_t edge = xadj_[vert]; edge < xadj_[vert+1]; ++edge){
   std::cout << " " << adjncy_[edge] << " [" << adjwgt_[edge] << "]";
  }
  std::cout << std::endl;
 }
 std::cout << std::flush;
 return;
}

} //namespace numerics

} //namespace exatn
