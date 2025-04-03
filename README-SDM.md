---
title: GAPBS Benchamark for Shared Disaggregated Memory for X86 ISA
shortdoc: >
    A modified GAPBS benchmark to separate graph allocation and graph kernels
    This work is based on the original GAPBS reference implementation by S. Beamer et al.
authors: ["kg"]
---

The original README of GAPBS can be found in `README.md`.

This work is based on GAPBS by S. Beamer et al. [1].
The version of GAPBS used is forked from https://github.com/darchr/gapbs
It already has gem5 hooks for architecture simulations.
Instead of a single system doing both the generation, allocation of the graph, and the graph kernel, we separate the allocation and the kernel.
The use case for this setup is in shared disaggregated memory (like CXL 3.0+), where there will be multiple hosts sharing memory.
Coherence across the hosts are software managed.
The allocator makes sure that the data is coherently written to the shared memory.
The reader hosts read the graph from the remote/shared memory and allocates data structures needed for doing the kernel locally.

The allocator can be configured to zero out the shared memory space in the beginning.
Further, the allocator always calls `clflushopt` to flush the entire shared memory region onto the memory, enforcing cache coherence.
Maybe in the future, this will be changed to a `M_SYNC`-like flag when the device is `open`.

See the example section on how to get started ASAP!

## Limitations

1. This version of GAPBS only work with synthetic graphs.
2. The graphs cannot have weights (sssp cannot be executed).

## An explanation on how GAPBS work

TL;DR: This section is only important if you want to understand/make changes to the source.

I hope this section will be a good documentation for people who want to make changes to the GAPBS benchmark.

1. Each graph kernel has a separate source file (`src/bc.cc` `src/bfs.cc` `src/cc.cc` `src/pr.cc` `src/tc.cc` `src/sssp.cc`)
2. A graph is generated for each kernel first, based on the input provided by the user (`defined in src/command_line.h`).
3. To understand how a new command line argument is added, see the comments for specifying the host ID (-x).
4. The file `src/builder.cc` is pretty much responsible for creating the graph.
    1. There are a bunch of utility functions to get the meta-data of the graph.
    2. `pvector` is the data structures that GAPBS use everywhere which is defined in `src/pvector.h`.
    3. The template `DestID_` stores most of the graph information in *active* memory.
    4. The first method called in builder class (`BuilderBase`; will just refer to as the builder class) to create a graph is `MakeGraph()`.
    5. An `EdgeList` data type is used to create and store edge information of the graph.
    6. If you want to use a synthetic graph, the Generator class' `GenerateEL()` method randomly generates an edge list.
    7. In my understanding, the version of GAPBS that I used (from/for gem5), the random seeds are the same, making the graph same across different runs.
    8. The method `MakeGraphFromEL()` is called with the el object, which was just created.
    9. The variable `el` is simply passed around until the neighbors (neighs, `DestID_`) and index (index, `DestID_` x `DestID_`) arrays are generated.
    10. The neighs and index are created in `MakeGraphFromEL()` as null pointers in the beginning.
    11. A synthetically generated graph will NOT have the inv_* variables used (unless specified I guess).
    12. A graph read from a file WILL have them set.
    13. `needs_weights` is pretty much relevant for `sssp`.
    14. `MakeCSR()` method assigns values to the neighs and index arrays.
    15. The size of neighs is `offsets[num_nodes_]`.
    16. The number of rows that index has is `offsets.size()`.
    17. The length of each row is variable.
    18. `MakeCSR()` method creates an unoptimized copy of the graph in the memory.
    19. The flow then goes back to the `MakeGraph()` method.
    20. The `el` variable is destroyed as soon as the arrays are stored in the memory.
    21. The Graph class object `g` is initially initialized to pretty much NULL (see `src/graph.h` `Graph::CSRGraph()` constructor).
    22. Command line parameters that are relevant are: graph file name (filename()), uniform(), `in_place_`, directed(), and weights.
    23. There is an extra scope added to make sure that the `EdgeList` object `el` is deleted after the arrays are generated for the first time.
    24. From what I understand, the Graph object stores the neighs and index within its scope.
    25. `SquishGraph(Graph object)` is called with the Graph object `g`, which then calls `SquishCSR` method since the graph is in CSR format.
    26. Note that there are DestID_ pointers to the neighs and index arrays, which are used to squish the graph.
    27. By squishing (in `SquishCSR()`), the author means to create an *optimized* copy of the graph to store in the memory (I might be wrong).
    28. The final versions of neighs and index are generated in `SquishCSR()` method.
    29. The builder class' objectives are done.
5. The graph's structure as a class is written in Graph class.
    1. The flow then goes to the Graph class in `src/graph.c`.
    2. The specific constructor that gets called is `CSRGraph::CSRGraph(int64_t num_nodes, DestID_** index, DestID_* neighs)` for this specific example.
    3. The in_* and out_* variables are the same in this case as the graph is undirected.
    4. The number of rows in index is +1 than the number of nodes.
    5. The number of edges *should* be half of the neighs' size.
    6. The number of edges is calculated as the difference of the last + 1 row of the index array and the base of the same array.
6. The algorithm then kicks in!

## Making GAPBS disaggregated!

TL;DR: The idea here to separate the allocation and the kernel part of the benchmark.

Here is what I did:
1. A new command line parameter `-x` is added to allow the user to specify whether a given host is a writer (allocator) or a worker (reader) node.
2. This is defined in `command_line.h` file. See the comments for more details.
3. TODO: The allocator *only* allocates the graph. In the current implementation, the allocator also executes a kernel (low priority, but I'll separate them).
4. Suppose the allocator host (referred to as the writer) wants to allocate and run `bfs`, it'll generate the edge list and create the neighs and index arrays.
5. Instead of simply starting the kernel, the Graph class is extended to write the graph into a shared memory space.
6. For single host testing, we use Linux shmem.
7. An allocator specific file is added to the source in `src/dmalloc.h`.
8. Function `dmalloc(size, host_id)` mounts the shared memory region in the /dev/dax region in dmalloc.h, `hmalloc(size, host_id)` uses huge pages to allocate a 1 GiB huge page for the allocation and `shmalloc(size, host_id)` uses shmem.
9. The Graph class constructor will store the pointer to the shared memory region as a private class variable.
10. The builder class is modified such that only the writer is allowed to generate the edge list, populate the neighs and index arrays and squish the CSR graph.
11. The worker nodes (or hosts or processes) initialized the Graph class object `g` as an empty object.
12. In both cases, the Graph constructor is called with host information.
13. The pointer to the mmapped region initialized when the Graph class constructor with the final neighs and index is created (at the end of the builder class).
14. If the writer calls the constructor, it writes some metadata, the neigh array, length of each row of the index array and finally a 2-D index array. See Figure 1 for a better visualization.
15. The `synch. variable` is unset when the allocator is writing the graph. It is set only when everything is written into the shared memory region.
16. The worker nodes will wait until the graph is fully written into the shared memory region (pooling into the (int) of the start of the mmap pointer). A potential optimization is to use something like `m_wait`.
17. The allocator needs to have the `neigh_size` as the length of C pointers are not defined. Same thing for the index array. This is passed as an argument to the class constructor.
18. If the aforementioned values are undefined or -1 and the host ID > 0 (aka worker node), the sizes are read out of the shared memory.
19. The writer figures out the size of each row of the index array and then writes them into the shared memory (column size for each index row array).
20. The reader reads out the same values before assigning pointers to the index array.
21. In both of these cases, the neighs are read out from `out_neighbors_` class variable and the index is read out from `in_index_` class variable when performing kernel related operations.

```
Figure 1. Map of the mmapped region.
__________________________________________________________________________________________________________________________________________________ .. __
|                 |           |              |            |         |                                 |          |    |          |            |        |
| synch. variable | num_nodes | index_x size | neigh_size | *neighs | *column size for each index row | index[0] | .. | index[N] | index[N+1] | unused |
|                 |           |              |            |  array  |               array             |   row 0  |    |   row N  |   row N+1  |        |
|                 |           |              |            |         |                                 |__________|____|__________|____________|        |
|      int        |  int64_t  |    size_t    |   size_t   | DestID_ |               size_t            |         DestID_ x DestID_             |        |
|_________________|___________|______________|____________|_________|_________________________________|_______________________________________|________|
<---------------- metadata of the graph -----------------><-------------------------------- graph data -------------------------------------->
```

## Example

To quickly test the benchmark, use `shmalloc` for shared memory.
This is defined in `src/graph.h`.

A kernel can also allocate the graph, **HOWEVER**, it is recommended to use the `allocator` to allocate the graph.
Here are the new options added in this version of GAPBS.
```sh 
 -S <int>    : specify the size in GiB of the shared memory     # Eg. 1, 2, 3 .. (2^31 - 1)                   
 -T <int>    : enable test mode /dev/shmem is mounted [0]/1     # Only use this when testing the framework on a single host system. Defaults to [0]           
 -l <int>    : if you want to zero out the memory [0]/1         # The allocator can explicitly zero out the bits of the shared memory region before allocating.
 -x <int>    : specify the host id. 0 -> master                 # Host ID >= 0 (master/allocator), 1 .. n (other hosts)
 ```

To allocate a graph:
```sh
./allocator -S 16 -l 1 -x 0 -g 20
# The shmem region is unset initially and then not unset at the end
```

Any other process can be created for the other graph kernels, what will read the same graph from the shared memory region.
```sh
./bfs -S 16 -x 1 -g 20 &
./cc -S 16 -x 2 -g 20 &
./cc_sv -S 16 -x 3 -g 20 &
./bc -S 16 -x 4 -g 20 &
./pr -S 16 -x 5 -g 20 &
./tc -S 16 -x 6 -g 20 &
```
## Using this framework in simulations

TODO.

## References
[1] S. Beamer, K. Asanovi´ c, and D. Patterson, “The GAP Benchmark Suite,” arXiv preprint arXiv:1508.03619, 2015.