// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

// This is the allocator program that allocates a graph.

#include <iostream>
#include <vector>

#include "benchmark.h"
#include "command_line.h"
#include "dmalloc.h"

int main(int argc, char* argv[]) {
  CLApp cli(argc, argv, "allocator");
  if (!cli.ParseArgs())
    return -1;
  Builder b(cli);
  // kg: invoking MakeGraph with a valid host_id
  assert(cli.host_id() >= 0);
  Graph g = b.MakeGraph(cli.host_id());

  // pretty much done here!
  // we don't overwrite the graph or munmap it unless the user specifies.
  if (cli.munmap_me() == 1) {
      std::cout << "info: zeroing out the memory" << std::endl;
      munmap_memory();
  }

  return 0;
}


