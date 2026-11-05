#include "coriolis/parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace coriolis {

namespace {

/// Set while a thread is running a piece of a `ParallelRows` job, so that a job started from inside one is run in place
thread_local bool t_inside = false;

int ChooseThreads() {
  int n = static_cast<int>(std::thread::hardware_concurrency());
  if (n <= 0) {
    n = 1;
  }
  n = std::min(n, 16);
  if (const char* env = std::getenv("CORIOLIS_THREADS")) {
    const int wanted = std::atoi(env);
    if (wanted >= 1) {
      n = std::min(wanted, 64);
    }
  }
  return n;
}

/// One job: the rows to do, in pieces that threads take one after another. A job is never changed once it has been
/// handed to the threads, and a thread that is late to it just finds no pieces left.
struct Job {
  const std::function<void(int, int)>* body = nullptr;
  int end = 0;
  int chunk = 1;
  std::atomic<int> next{0};
  std::atomic<int> pending{0};
};

/// A pool of threads that wait for row jobs. The caller of a job takes part in it too.
class Pool {
 public:
  Pool() : threads_(ChooseThreads()) {
    for (int i = 1; i < threads_; ++i) {
      workers_.emplace_back([this] { Work(); });
    }
  }

  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : workers_) {
      t.join();
    }
  }

  int threads() const { return threads_; }

  void Run(int begin, int end, const std::function<void(int, int)>& body, int chunk) {
    // One job at a time: a second caller on another thread waits its turn
    std::lock_guard<std::mutex> submit(submit_mutex_);
    auto job = std::make_shared<Job>();
    job->body = &body;
    job->end = end;
    job->chunk = chunk;
    job->next.store(begin);
    job->pending.store((end - begin + chunk - 1) / chunk);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_ = job;
      ++generation_;
    }
    wake_.notify_all();
    Take(*job);
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return job->pending.load() == 0; });
    current_.reset();
  }

 private:
  /// Take pieces of a job until there are none left
  void Take(Job& job) {
    const bool was_inside = t_inside;
    t_inside = true;
    for (;;) {
      const int first = job.next.fetch_add(job.chunk);
      if (first >= job.end) {
        break;
      }
      (*job.body)(first, std::min(first + job.chunk, job.end));
      if (job.pending.fetch_sub(1) == 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.notify_all();
      }
    }
    t_inside = was_inside;
  }

  void Work() {
    unsigned long seen = 0;
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) {
          return;
        }
        seen = generation_;
        job = current_;
      }
      if (job) {
        Take(*job);
      }
    }
  }

  int threads_;
  std::vector<std::thread> workers_;
  std::mutex submit_mutex_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  std::shared_ptr<Job> current_;
  unsigned long generation_ = 0;
  bool stop_ = false;
};

Pool& ThePool() {
  static Pool pool;
  return pool;
}

}  // namespace

int ParallelThreads() { return ThePool().threads(); }

void ParallelRows(int begin, int end, const std::function<void(int, int)>& body, int min_rows) {
  if (end <= begin) {
    return;
  }
  Pool& pool = ThePool();
  if (t_inside || pool.threads() <= 1 || end - begin < std::max(2, min_rows)) {
    body(begin, end);
    return;
  }
  // Several pieces to a thread, so a slow piece does not leave the others waiting
  const int pieces = pool.threads() * 4;
  const int chunk = std::max(1, (end - begin + pieces - 1) / pieces);
  pool.Run(begin, end, body, chunk);
}

}  // namespace coriolis
