// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef GENERATOR_H_
#define GENERATOR_H_

#include <algorithm>
#include <cinttypes>
#include <random>
#include <iostream>

#include "graph.h"
#include "pvector.h"
#include "util.h"
#include <boost/random/uniform_real_distribution.hpp>

/*
GAP Benchmark Suite
Class:  Generator
Author: Scott Beamer

Given scale and degree, generates edgelist for synthetic graph
 - Intended to be called from Builder
 - GenerateEL(uniform) generates and returns the edgelist
 - Can generate uniform random (uniform=true) or R-MAT graph according
   to Graph500 parameters (uniform=false)
 - Can also randomize weights within a weighted edgelist (InsertWeights)
 - Blocking/reseeding is for parallelism with deterministic output edgelist
*/


template <typename NodeID_, typename DestID_ = NodeID_,
          typename WeightT_ = NodeID_>
class Generator {
  typedef EdgePair<NodeID_, DestID_> Edge;
  typedef EdgePair<NodeID_, NodeWeight<NodeID_, WeightT_>> WEdge;
  typedef pvector<Edge> EdgeList;

 public:
  // kg: To be backward compatible, we are throwing an error when the user
  // wants to invoke gapbs without specifying a node id.
  Generator(int scale, int degree) {
      // kg: this fatally fails!
      std::cout << "fatal: This is the disaggregated version of gapbs. " <<
          "Please specify a node ID when invoking gapbs algorithms." <<
          std::endl;
      // The program is killed. All resources *should be* automatically
      // released. Maybe?
      // TODO
      exit(-1);
  }
  Generator(int scale, int degree, int host_id) {
    // kg: add a new variable to hold the host_id_
    host_id_ = host_id;
    // kg: make sure that the node ID is valid. Can't do much, but the value
    // must always be >= 0.
    if (host_id_ < 0) {
        std::cout << "fatal: invalid node ID. Must be a positive number!" <<
                                                                    std::endl;
        exit(-1);
    }
    else if (host_id_ == 0)
        // inform the user the host/node id and the role
        std::cout << "info: This host is the master node!" << std::endl; 
    else
        std::cout << "info: This host is a worker node." << std::endl;

    scale_ = scale;
    num_nodes_ = 1l << scale;
    num_edges_ = num_nodes_ * degree;
    if (num_nodes_ > std::numeric_limits<NodeID_>::max()) {
      std::cout << "NodeID type (max: " << std::numeric_limits<NodeID_>::max();
      std::cout << ") too small to hold " << num_nodes_ << std::endl;
      std::cout << "Recommend changing NodeID (typedef'd in src/benchmark.h)";
      std::cout << " to a wider type and recompiling" << std::endl;
      std::exit(-31);
    }
  }

  void PermuteIDs(EdgeList &el) {
    pvector<NodeID_> permutation(num_nodes_);
    std::mt19937 rng(kRandSeed);
    #pragma omp parallel for
    for (NodeID_ n=0; n < num_nodes_; n++)
      permutation[n] = n;
    shuffle(permutation.begin(), permutation.end(), rng);
    #pragma omp parallel for
    for (int64_t e=0; e < num_edges_; e++)
      el[e] = Edge(permutation[el[e].u], permutation[el[e].v]);
  }

  EdgeList MakeUniformEL() {
    EdgeList el(num_edges_);
    #pragma omp parallel
    {
      std::mt19937 rng;
      std::uniform_int_distribution<NodeID_> udist(0, num_nodes_-1);
      #pragma omp for
      for (int64_t block=0; block < num_edges_; block+=block_size) {
        rng.seed(kRandSeed + block/block_size);
        for (int64_t e=block; e < std::min(block+block_size, num_edges_); e++) {
          el[e] = Edge(udist(rng), udist(rng));
        }
      }
    }
    return el;
  }

  EdgeList MakeRMatEL() {
    const float A = 0.57f, B = 0.19f, C = 0.19f;
    EdgeList el(num_edges_);
    #pragma omp parallel
    {
      static std::mt19937 rng;
      static boost::random::uniform_real_distribution<> udist(0, 1.0f);
      #pragma omp threadprivate(rng , udist)
      #pragma omp for
      for (int64_t block=0; block < num_edges_; block+=block_size) {
        rng.seed(kRandSeed + block/block_size);
        for (int64_t e=block; e < std::min(block+block_size, num_edges_); e++) {
          NodeID_ src = 0, dst = 0;
          for (int depth=0; depth < scale_; depth++) {
            float rand_point = udist(rng);
            src = src << 1;
            dst = dst << 1;
            if (rand_point < A+B) {
              if (rand_point > A)
                dst++;
            } else {
              src++;
              if (rand_point > A+B+C)
                dst++;
            }
          }
          el[e] = Edge(src, dst);
        }
      }
    }
    PermuteIDs(el);
    // TIME_PRINT("Shuffle", std::shuffle(el.begin(), el.end(),
    //                                    std::mt19937()));
    return el;
  }

  EdgeList GenerateEL(bool uniform) {
    EdgeList el;
    Timer t;
    t.Start();
    if (uniform)
      el = MakeUniformEL();
    else
      el = MakeRMatEL();
    t.Stop();
    PrintTime("Generate Time", t.Seconds());
    return el;
  }

  static void InsertWeights(pvector<EdgePair<NodeID_, NodeID_>> &el) {}

  // Overwrites existing weights with random from [1,255]
  static void InsertWeights(pvector<WEdge> &el) {
    #pragma omp parallel
    {
      std::mt19937 rng;
      std::uniform_int_distribution<int> udist(1, 255);
      int64_t el_size = el.size();
      #pragma omp for
      for (int64_t block=0; block < el_size; block+=block_size) {
        rng.seed(kRandSeed + block/block_size);
        for (int64_t e=block; e < std::min(block+block_size, el_size); e++) {
          el[e].v.w = static_cast<WeightT_>(udist(rng));
        }
      }
    }
  }

 private:
  // kg: adding another variable to hold the node id in the generator function.
  int host_id_;
  int scale_;
  int64_t num_nodes_;
  int64_t num_edges_;
  static const int64_t block_size = 1<<18;
};

#endif  // GENERATOR_H_
