// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef GRAPH_H_
#define GRAPH_H_

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <iostream>
#include <type_traits>

#include <fstream>

#include "pvector.h"
#include "util.h"

// kg: adding a dmalloc header to enable disaggregated memory allocations.
// note that this is NOT an allocator in the traditional sense. We need a map
// of a memory, not an entire fully-fledged memory allocator.
#include "dmalloc.h"

#define INT sizeof(int)
#define SIZE_T sizeof(size_t)
#define DESTID_ sizeof(DestID_)

/*
GAP Benchmark Suite
Class:  CSRGraph
Author: Scott Beamer

Simple container for graph in CSR format
 - Intended to be constructed by a Builder
 - To make weighted, set DestID_ template type to NodeWeight
 - MakeInverse parameter controls whether graph stores its inverse
*/


// Used to hold node & weight, with another node it makes a weighted edge
template <typename NodeID_, typename WeightT_>
struct NodeWeight {
  NodeID_ v;
  WeightT_ w;
  NodeWeight() {}
  NodeWeight(NodeID_ v) : v(v), w(1) {}
  NodeWeight(NodeID_ v, WeightT_ w) : v(v), w(w) {}

  bool operator< (const NodeWeight& rhs) const {
    return v == rhs.v ? w < rhs.w : v < rhs.v;
  }

  // doesn't check WeightT_s, needed to remove duplicate edges
  bool operator== (const NodeWeight& rhs) const {
    return v == rhs.v;
  }

  // doesn't check WeightT_s, needed to remove self edges
  bool operator== (const NodeID_& rhs) const {
    return v == rhs;
  }

  operator NodeID_() {
    return v;
  }
};

template <typename NodeID_, typename WeightT_>
std::ostream& operator<<(std::ostream& os,
                         const NodeWeight<NodeID_, WeightT_>& nw) {
  os << nw.v << " " << nw.w;
  return os;
}

template <typename NodeID_, typename WeightT_>
std::istream& operator>>(std::istream& is, NodeWeight<NodeID_, WeightT_>& nw) {
  is >> nw.v >> nw.w;
  return is;
}



// Syntatic sugar for an edge
template <typename SrcT, typename DstT = SrcT>
struct EdgePair {
  SrcT u;
  DstT v;

  EdgePair() {}

  EdgePair(SrcT u, DstT v) : u(u), v(v) {}

  bool operator< (const EdgePair& rhs) const {
    return u == rhs.u ? v < rhs.v : u < rhs.u;
  }

  bool operator== (const EdgePair& rhs) const {
    return (u == rhs.u) && (v == rhs.v);
  }
};

// SG = serialized graph, these types are for writing graph to file
typedef int32_t SGID;
typedef EdgePair<SGID> SGEdge;
typedef int64_t SGOffset;



template <class NodeID_, class DestID_ = NodeID_, bool MakeInverse = true>
class CSRGraph {
  // Used for *non-negative* offsets within a neighborhood
  typedef std::make_unsigned<std::ptrdiff_t>::type OffsetT;

  // Used to access neighbors of vertex, basically sugar for iterators
  class Neighborhood {
    // kg: For some reasons the neighbors are not getting accessed correctly.
    NodeID_ n_;
    DestID_** g_index_;
    OffsetT start_offset_;
   public:
    Neighborhood(NodeID_ n, DestID_** g_index, OffsetT start_offset) :
        n_(n), g_index_(g_index), start_offset_(0) {
      OffsetT max_offset = end() - begin();
      start_offset_ = std::min(start_offset, max_offset);
    }
    typedef DestID_* iterator;
    iterator begin() { return g_index_[n_] + start_offset_; }
    iterator end()   { return g_index_[n_+1]; }
  };

  void ReleaseResources() {
    // These graphs aren't allocated using malloc or new. so we can skip these
    // for now.
    /*
    if (out_index_ != nullptr)
      delete[] out_index_;
    if (out_neighbors_ != nullptr)
      delete[] out_neighbors_;
    if (directed_) {
      if (in_index_ != nullptr)
        delete[] in_index_;
      if (in_neighbors_ != nullptr)
        delete[] in_neighbors_;
    }
    */
  }


 public:
  CSRGraph() : directed_(false), num_nodes_(-1), num_edges_(-1),
    out_index_(nullptr), out_neighbors_(nullptr),
    in_index_(nullptr), in_neighbors_(nullptr) {}

  CSRGraph(int64_t num_nodes, DestID_** index, DestID_* neighs) :
    directed_(false), num_nodes_(num_nodes),
    out_index_(index), out_neighbors_(neighs),
    in_index_(index), in_neighbors_(neighs) {
      // kg: relabel function
      std::cout << "relabel" << std::endl;
      // num_edges_ = (out_index_[num_nodes_] - out_index_[0]) / 2;
    }

  // kg: we need our own CSRGraph constructor that uses the host_ids correctly.
  // further, a method is required to validate whether the graph generated and
  // read from the /dev is the same.
  // TODO
  CSRGraph(int64_t num_nodes, DestID_*** index, size_t index_x, size_t index_y,
        DestID_** neighs, size_t neigh_size, int host_id, bool validate_graph) :
    directed_(false), num_nodes_(num_nodes) {
      // kg: Make sure that the shared graph will be stored here and nothing
      // else.
      // goal: if this is the host then copy the contencts of the arrays to the
      // the mmapped backed memory and notify the user.

      // otherwise, return the pointers to the right part of the memory back to
      // the user as these might be clients. the pointer must be int*
      // _mmap_pointer = hmalloc(1 << 30, host_id);

      _mmap_pointer = shmalloc(1 << 30, host_id);

      assert(index_x == index_y);

      // kg: if i am a worker node, then I need to wait for the synch variable

      // the start of the mmap array will be allocated to the start of index
      _synch_var = (int *) &_mmap_pointer[0];

      if (host_id > 0) {
        std::cout << "info: waiting for master!" << std::endl;
        while (_synch_var[0] != 1) {}
        std::cout << "info: master has set the variable!" << std::endl;
      }
      
      std::cout << "info: special malloc was called! sane ver: " << index_x << " " << index_y << " " << neigh_size << std::endl;
      // std::cout << "value @ sync = " << _mmap_pointer[0] << std::endl;
      // need to assign size and data
      assign_sizes(_mmap_pointer, index_x, index_y, neigh_size, host_id);
      
      if (host_id == 0) {
        // The master needs to allocate and then populate the data.
        assign_data(_mmap_pointer, *index, index_x, index_y, *neighs, neigh_size);
       
      }

      // update: We don't need to free these data structures in the allocator.
      // The allocator will do some algo as well.

      // kg: once the data is assigned to the mmap space, we can freeup the
      // arrays that the program uses. THIS IS INCORRECT!!
      // int dummy;
      // std::cin >> dummy;
      // free(index);
      // free(neighs);

      // update: In case this is a worker node, it'll read the index and the
      // neighs from the mmap and then allocate the correct data into these
      // variables. ** This is not the right place **
      else if (host_id > 0) {

        // TODO: Now read the index and the neighs from the pointer. This needs
        // to work for the host and also for the workers as well. index and
        // neighs are both free pointers right now. Just reassign them from the
        // mmaped region
        // assign_index_and_neighs(_mmap_pointer, index, index_x, neighs);
        // for (size_t i = 0 ; i < index_x ; i++)
          // out_index_ = index[i];
        // *out_neighbors_ = neighs;
        // *in_neighbors_ = neighs;
        // if (index == nullptr) {
        //   std::cout << "null!" << std::endl;
        // }
        // *index = (DestID_ **) malloc (index_x * sizeof(DestID_ *));
        // for (size_t i = 0 ; i < index_x ; i++)
        //   for (size_t j = 0 ; j < index_index_array[i]; j++)
        //     index[i][j] = &out_index_[i][j];
        index = &out_index_;
        neighs = &out_neighbors_;

        // probe_data(index_x);
        
      }

        // offset_reader(_mmap_pointer);

      // print_data(index_x);
      // offset_reader(_mmap_pointer, 0, index_x, neigh_size);
      // print_neighs(neigh_size);
      std::cout << "value @ sync = " << _mmap_pointer[0] << std::endl;

      // kg: sanity check. make sure that the graph stored in the mmap space
      // is indeed the graph as generated.
      std::cout << "out index of the host " << host_id << " : " << *out_index_[num_nodes_] << " " << *out_index_[0] << " " << **index[0] << " " << num_nodes_ << std::endl;
      if (validate_graph == true) {
        // TODO
        std::cout << "fatal: NotImplementedError: Validation is pending!" <<
            std::endl;
        exit(-1);

      }
      // num_edges_ = 10496;
      num_edges_ = (out_index_[num_nodes_] - out_index_[0]) / 2;
      std::cout << "updated nodes: " << num_nodes_ << " edges: " << num_edges_
          << std::endl;

    }

  // kg: we need our own CSRGraph constructor that uses the host_ids correctly.
  // further, a method is required to validate whether the graph generated and
  // read from the /dev is the same.

  CSRGraph(int64_t num_nodes, DestID_** out_index, DestID_* out_neighs,
        DestID_** in_index, DestID_* in_neighs) :
    directed_(true), num_nodes_(num_nodes),
    out_index_(out_index), out_neighbors_(out_neighs),
    in_index_(in_index), in_neighbors_(in_neighs) {
      num_edges_ = out_index_[num_nodes_] - out_index_[0];
    }

  CSRGraph(CSRGraph&& other) : directed_(other.directed_),
    num_nodes_(other.num_nodes_), num_edges_(other.num_edges_),
    out_index_(other.out_index_), out_neighbors_(other.out_neighbors_),
    in_index_(other.in_index_), in_neighbors_(other.in_neighbors_) {
      other.num_edges_ = -1;
      other.num_nodes_ = -1;
      other.out_index_ = nullptr;
      other.out_neighbors_ = nullptr;
      other.in_index_ = nullptr;
      other.in_neighbors_ = nullptr;
  }

  ~CSRGraph() {
    ReleaseResources();
  }

  // kg: defining new methods to manage the memory
  void assign_sizes(int *_mmap, size_t index_x, size_t index_y, size_t neigh_size, int host_id) {
    // need to assign 2d pointers. regardless of the host_id, this step is true
    // for all hosts.
    // For grid allocation, IDK how to do a mismatch of index_x and index_y
    // assert(index_x == index_y);

    out_neighbors_ = (DestID_ *) &_mmap[1]; //  + index_x * index_y]; // * sizeof(DestID_)];
    in_neighbors_ = out_neighbors_;

    // next we need to keep a track of all the y indices
    index_index_array = (size_t *) &_mmap[1 + (neigh_size * DESTID_)/INT];


    // This is a 2d array. we need a holder for the pointer pointer
    out_index_ = (DestID_ **) malloc (index_x * sizeof(DestID_ *));
    in_index_ = (DestID_ **) malloc (index_x * sizeof(DestID_ *));

    // out_index_ = in_index_ = (DestID_ **) &_mmap[1];

    // in_index_ = (DestID_ **) &_mmap[1];
    if (host_id == 0) {
      // man this is complicated! assign_Data already do this

      // do nothing
      for (size_t i = 0 ; i < index_x ; i++) {
        // don't assign data but assign the sizes
        // out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT + (index_x * SIZE_T)/INT + (i * index_y * DESTID_)/INT];
        // std::cout << "out index at i " << i << " " << out_index_[i] << " " << 1 + neigh_size + index_x + i * index_y << std::endl;
        // in_index_[i] = out_index_[i];
        // in_index_[i] = (DestID_ *) &_mmap[i * index_y];
      }
    }
    else {
      // now that i have the sizes, i can reassign them. remember the first
      // element will start at the base i.e. +0!
      out_index_[0] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT +
                                                      (index_x * SIZE_T)/INT];
      // the in_index_ is the same for these graphs! I'm doing another line for
      // this step for better understanding.
      in_index_[0] = out_index_[0];


      // WE ONLY CARE ABOUT INDEX_X - 1 i.e. NUM_NODES rows!
      // A new variable local_sum is needed to figure out the length of each
      // row
      size_t sum_local = 0;
      std::cout << "info: the allocator is writing the index array to the shared"
              << " memory! This may take some time..." << std::endl;
      
      #pragma omp parallel for private(sum_local)
      for (size_t i = 1 ; i < index_x ; i++) {
        // make sure that the local sum is set to 0 for each loop.
        sum_local = 0;
        for (size_t j = 0 ; j < i ; j++) {
          sum_local += index_index_array[j];
        }
        // TODO
        // can there be a node without any edges? for an undirected graph, NO
        // sanity check
        assert(sum_local != 0);
        out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT +
                            (index_x * SIZE_T)/INT + (sum_local * DESTID_)/INT];
        // finally set the in_index_ value
        in_index_[i] = out_index_[i];
      }

      /*
      #pragma omp parallel for
      for (size_t i = 0 ; i < index_x ; i++) {
        size_t sum = index_index_array[0];
        

        #pragma omp parallel for
        for (size_t j = 1 ; j <= i ; j++)
          sum += index_index_array[j];
        // sanity check
        assert(sum != 0);
        out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT + (index_x * SIZE_T)/INT + (sum * DESTID_)/INT]; // index_index_array[i] * sizeof(DestID_)];

        // std::cout << "out index at i " << i << " " << out_index_[i] << " " << 1 + (neigh_size * DESTID_)/INT + (index_x * SIZE_T)/INT + (sum * DESTID_)/INT << std::endl;
        // std::cout << index_index_array[i] << " " << sum - index_index_array[0] << " "  << out_index_[i] << " " << out_index_[i] - out_index_[0] << std::endl;
        in_index_[i] = out_index_[i];
      }
      */
    }

    // assign neigh starting from the end of the index array.
    std::cout << "assign neighs: " << index_x << " " << index_y << " " << index_x * index_y << " " << neigh_size << std::endl;
    std::cout << "ptr: " << _mmap << " " << out_neighbors_ << " " << out_index_ << " " <<  out_index_[1024] - out_index_[0] << std::endl;
  

    // in_neighbors_ = (DestID_ *) &_mmap[index_x * index_y];

    // this data should already be allocated.
    
    // make sure to allocate 

  }

  void swap(size_t old_x, size_t old_y, size_t new_x, size_t new_y) {
    auto value_at_old_index = out_index_[old_x][old_y];
    auto value_at_new_index =  out_index_[new_x][new_y];

    std::cout << value_at_old_index << " " << value_at_new_index << std::endl;

     out_index_[new_x][new_y] = out_index_[old_x][old_y];
  }

  void worker_index_read(size_t index_x) {
    // resize the array
    for (size_t i = 0 ; i < index_x - 1 ; i++)
      for (size_t j = 1 ; j <= index_index_array[i]; j++)
        out_index_[i][j] = *out_index_[i * index_x + j];
  }

  void assign_data(int *_mmap, DestID_** index, size_t index_x, size_t index_y,
    DestID_ *neighs, size_t neigh_size) {
    // once every sinvle variable size is set, we only need to assign the data
    // only the master calls this function
    
    // kg: the problem here is that each row of the index array is of variable
    // size. The client/worker has no way of knoing this unless this
    // information is stored in the shared memory.

    // The index_index_array needs to store the number of elements each
    // row has, i.e. elements_in_row_i
    // This can be calculated by subtrating the base of the i th row
    // from i + 1 th row.
    // The final index is not necessary as there are index_x - 1 number
    // of nodes.
    #pragma omp parallel for
    for (size_t i = 0 ; i < index_x - 1; i++) {
      // std::cout << "out of i " << out_index_[i] << " diff " << std::endl;
      // TODO
      // the next index needs to be stored in the shmem. No need to type cast
      // as this will be stored as a size_t element.
      index_index_array[i] = (index[i + 1] - index[i]);
    }

    // now that i have the sizes, i can reassign them. remember the first elem-
    // ent will start at the base i.e. +0!
    out_index_[0] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT +
                                                      (index_x * SIZE_T)/INT];
    // the in_index_ is the same for these graphs! I'm doing another line for
    // this step for better understanding.
    in_index_[0] = out_index_[0];

    // WE ONLY CARE ABOUT INDEX_X - 1 i.e. NUM_NODES rows!
    // A new variable local_sum is needed to figure out the length of each row
    size_t sum_local = 0;
    std::cout << "info: the allocator is writing the index array to the shared"
            << " memory! This may take some time..." << std::endl;

    #pragma omp parallel for private(sum_local)
    for (size_t i = 1 ; i < index_x ; i++) {
      // make sure that the local sum is set to 0 for each loop.
      sum_local = 0;
      for (size_t j = 0 ; j < i ; j++) {
        sum_local += index_index_array[j];
      }
      // TODO
      // can there be a node without any edges? for an undirected graph, NO
      // sanity check
      assert(sum_local != 0);
      out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT +
                           (index_x * SIZE_T)/INT + (sum_local * DESTID_)/INT];
      // finally set the in_index_ value
      in_index_[i] = out_index_[i];
    }

    // set the final element!
    // sum_local = 0;
    //   for (size_t j = 0 ; j < index_x ; j++) {
    //     sum_local += index_index_array[j];
    //   }
    // out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT +
    //                        (index_x * SIZE_T)/INT + (sum_local * DESTID_)/INT];
    


    /*
    size_t sum_local = 0;
    #pragma omp parallel for private(sum_local)
    for (size_t i = 1 ; i < index_x ; i++) {
      sum_local = index_index_array[0];
      // size_t sum = index_index_array[0];
      
      if (i % 100000 == 0)
        std::cout << " 10 % " << std::endl;
      for (size_t j = 1 ; j <= i ; j++)
        sum_local += index_index_array[j];
      // sanity check
      if (sum_local == 0) {
        std::cout << "fatal: zero sum seen at i = " << i << std::endl;
      }
      // assert(sum != 0);
      out_index_[i] = (DestID_ *) &_mmap[1 + (neigh_size * DESTID_)/INT + (index_x * SIZE_T)/INT + (sum_local * DESTID_)/INT]; // index_index_array[i] * sizeof(DestID_)];

      // std::cout << "out index at i " << i << " " << out_index_[i] << " " << 1 + (neigh_size * DESTID_)/INT + (index_x * SIZE_T)/INT + (sum * DESTID_)/INT << std::endl;
      // std::cout << index_index_array[i] << " " << sum - index_index_array[0] << " "  << out_index_[i] << " " << out_index_[i] - out_index_[0] << std::endl;
      in_index_[i] = out_index_[i];
    }
    */

    // make sure that the index is written properly!
    #pragma omp barrier

    // ready to write the memory!!!
    #pragma omp parallel for
    for (size_t i = 0 ; i < index_x - 1; i++) {

      for (size_t j = 0; j < index_index_array[i]; j++)
        out_index_[i][j] = index[i][j];
      
      // making absolutely sure at this point.
      in_index_[i] = out_index_[i];
    }

    // this is the simple step of writing an one-dimensional array
    for (size_t i = 0 ; i < neigh_size ; i++) {
      out_neighbors_[i] = neighs[i];
      in_neighbors_[i] = neighs[i];
    }

    // make sure that the synchronization variable is finally set
    _synch_var[0] = 1;
  }

  void probe_data(size_t index_x) {
    // kg: must be called by a worker node!
    for (size_t i = 0 ; i < index_x - 1 ; i++) {
      for (size_t j = 1 ; j <= index_index_array[i]; j++) {
        if (out_index_[i][j] == 164) {
          std::cout << "hhh " << i << " " << j << std::endl;
        }
        if (out_index_[i][j] != 0) {
          std::cout << "non zero at " << i << " " << j << " with " << out_index_[i][j] << std::endl;
        }
      }
    }
  }

void offset_reader(int *_mmap_pointer, size_t offset, size_t index_x, size_t neigh_size) {
    // the first element is an integer
    // the next element is of struct two
    // size_t y = offset % ;
    // size_t x = offset / DIMA;

    // effective offset offsets the offset by a factor TWO/SIZE_T.
    // This is because each element in struct two is of size_t and at
    // each offset, there are two elements.
    int size_factor = 1; // TWO/SIZE_T;

    // need to find the base of the array
    DestID_ *base = (DestID_ *) (_mmap_pointer + 1 + neigh_size + index_x); // + neigh_size * DESTID_/INT + index_x);

    // there are two elements per offset (aka index)
    for (int i = 0 ; i < neigh_size ; i++) {
      // as int/DestID_
      std::cout << (DestID_) (*(base + i)) << " ";
      if (i % 1000 == 0)
        std::cout << std::endl;
    }
    // std::cout << "(" << x << "," << y << ") :: @off = " << offset
    //         << " { a " << (size_t *) (base + effective_offset) << " = "
    //         << (size_t) (*(base + effective_offset))
    //         << " b " << (size_t *) (base + effective_offset + 1)
    //         <<  " = " << (size_t) (*(base + effective_offset + 1))
    //         << " }" << std::endl;
}

  void offset_reader(int *_mmap_pointer) {
      // does an int read
      for (int i = 0 ; i < 0x40000000 / INT ; i++)
        std::cout << "@off = " << i << " " << (int) (*(_mmap_pointer + i)) << std::endl;
  }

  void print_data(size_t index_x) {
    // kg: must be called by a worker node!
    for (size_t i = 0 ; i < index_x - 1 ; i++) {
      std::cout << i << "," << index_index_array[i] << "  ::  ";
      for (size_t j = 0 ; j < index_index_array[i]; j++) {
        
        std::cout <<  out_index_[i][j] << " ";
        
      }
      std::cout << std::endl;
    }
    std::cout << INT << " " << DESTID_ << " " << SIZE_T << std::endl;
  }

  void print_neighs(size_t neigh_size) {
    for (size_t i = 0 ; i < neigh_size ; i++) {
      std::cout << out_neighbors_[i] << " ";
      if (i % 1000 == 0)
        std::cout << std::endl;
    }
  }

  void assign_index_and_neighs(int *_mmap, DestID_** index, size_t index_x,
    DestID_ *neighs) {
    
    // Just assign the given pointers from the mmaped region.
    for (size_t i = 0 ; i < index_x ; i++)
      index[i] = out_index_[i];
    
    // reassign the neighs here.
    neighs = out_neighbors_;

  }

  CSRGraph& operator=(CSRGraph&& other) {
    if (this != &other) {
      ReleaseResources();
      directed_ = other.directed_;
      num_edges_ = other.num_edges_;
      num_nodes_ = other.num_nodes_;
      out_index_ = other.out_index_;
      out_neighbors_ = other.out_neighbors_;
      in_index_ = other.in_index_;
      in_neighbors_ = other.in_neighbors_;
      other.num_edges_ = -1;
      other.num_nodes_ = -1;
      other.out_index_ = nullptr;
      other.out_neighbors_ = nullptr;
      other.in_index_ = nullptr;
      other.in_neighbors_ = nullptr;
    }
    return *this;
  }

  bool directed() const {
    return directed_;
  }

  int64_t num_nodes() const {
    return num_nodes_;
  }

  int64_t num_edges() const {
    return num_edges_;
  }

  int64_t num_edges_directed() const {
    return directed_ ? num_edges_ : 2*num_edges_;
  }

  int64_t out_degree(NodeID_ v) const {
    return out_index_[v+1] - out_index_[v];
  }

  int64_t in_degree(NodeID_ v) const {
    static_assert(MakeInverse, "Graph inversion disabled but reading inverse");
    return in_index_[v+1] - in_index_[v];
  }

  Neighborhood out_neigh(NodeID_ n, OffsetT start_offset = 0) const {
    return Neighborhood(n, out_index_, start_offset);
  }

  Neighborhood in_neigh(NodeID_ n, OffsetT start_offset = 0) const {
    static_assert(MakeInverse, "Graph inversion disabled but reading inverse");
    return Neighborhood(n, in_index_, start_offset);
  }

  void PrintStats() const {
    std::cout << "Graph has " << num_nodes_ << " nodes and "
              << num_edges_ << " ";
    if (!directed_)
      std::cout << "un";
    std::cout << "directed edges for degree: ";
    std::cout << num_edges_/num_nodes_ << std::endl;
  }

  void PrintTopology() const {
    for (NodeID_ i=0; i < num_nodes_; i++) {
      std::cout << i << ": ";
      for (DestID_ j : out_neigh(i)) {
        std::cout << j << " ";
      }
      std::cout << std::endl;
    }
  }

  static DestID_** GenIndex(const pvector<SGOffset> &offsets, DestID_* neighs) {
    // x of index is offset.size()
    NodeID_ length = offsets.size();
    DestID_** index = new DestID_*[length];
    #pragma omp parallel for
    for (NodeID_ n=0; n < length; n++)
      index[n] = neighs + offsets[n];
    return index;
  }

  pvector<SGOffset> VertexOffsets(bool in_graph = false) const {
    pvector<SGOffset> offsets(num_nodes_+1);
    for (NodeID_ n=0; n < num_nodes_+1; n++)
      if (in_graph)
        offsets[n] = in_index_[n] - in_index_[0];
      else
        offsets[n] = out_index_[n] - out_index_[0];
    return offsets;
  }

  Range<NodeID_> vertices() const {
    return Range<NodeID_>(num_nodes());
  }

 private:
  int *_mmap_pointer;
  int *_synch_var;
  bool directed_;
  int64_t num_nodes_;
  int64_t num_edges_;
  size_t *index_index_array;
  DestID_** out_index_;
  DestID_*  out_neighbors_;
  DestID_** in_index_;
  DestID_*  in_neighbors_;
};

#endif  // GRAPH_H_
