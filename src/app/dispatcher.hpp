#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace lv::app {

/// Runs blocking jobs (network, disk) on worker threads and hands their results back to
/// the UI thread. post() wakes the UI's poll() through a pipe; the UI calls drain().
class Dispatcher {
public:
    using Job = std::function<void(const std::stop_token&)>;

    explicit Dispatcher(std::size_t threads = 4);
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;
    Dispatcher(Dispatcher&&) = delete;
    Dispatcher& operator=(Dispatcher&&) = delete;

    /// Queues a job for a worker thread.
    void run(Job job);
    /// Queues a callback for the UI thread (callable from any thread).
    void post(std::function<void()> fn);

    /// Readable when callbacks are waiting.
    [[nodiscard]] int wake_fd() const { return pipe_[0]; }
    /// Runs queued callbacks on the calling (UI) thread. Returns how many ran.
    std::size_t drain();

    /// Stops workers after their current job; queued jobs are dropped.
    void shutdown();

private:
    void worker(const std::stop_token& stop);

    std::mutex jobs_mutex_;
    std::condition_variable_any jobs_cv_;
    std::deque<Job> jobs_;

    std::mutex main_mutex_;
    std::vector<std::function<void()>> main_queue_;

    std::array<int, 2> pipe_{-1, -1};
    std::vector<std::jthread> workers_;
};

} // namespace lv::app
