#ifndef SOFTRENDER_PARALLEL_H_
#define SOFTRENDER_PARALLEL_H_

#include <functional>

namespace softrender {

/// How many threads share the work in `ParallelRows`, the calling thread included. It is the number of hardware threads
/// up to 16, or the number in the SOFTRENDER_THREADS environment variable (1 for none: everything then runs on the
/// calling thread).
int ParallelThreads();

/// Run `body(first, last)` over pieces of the rows [begin, end) on a pool of threads, and return when all of them are
/// done. The pieces never overlap, so `body` may write the rows it is given without a lock, but must not touch rows
/// that it was not given. Small jobs (fewer than `min_rows` rows), calls made from inside another `ParallelRows` and runs
/// with one thread are done on the calling thread, in order.
void ParallelRows(int begin, int end, const std::function<void(int, int)>& body, int min_rows = 16);

}  // namespace softrender

#endif  // SOFTRENDER_PARALLEL_H_
