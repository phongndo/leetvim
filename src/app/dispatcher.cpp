#include "app/dispatcher.hpp"

#include "util/log.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <exception>

namespace lv::app {

Dispatcher::Dispatcher(std::size_t threads) {
    if (pipe(pipe_.data()) == 0) {
        for (const int fd : pipe_) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
    }
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this](const std::stop_token& stop) { worker(stop); });
    }
}

Dispatcher::~Dispatcher() {
    shutdown();
    for (const int fd : pipe_) {
        if (fd >= 0) close(fd);
    }
}

void Dispatcher::run(Job job) {
    {
        const std::scoped_lock lock(jobs_mutex_);
        jobs_.push_back(std::move(job));
    }
    jobs_cv_.notify_one();
}

void Dispatcher::post(std::function<void()> fn) {
    {
        const std::scoped_lock lock(main_mutex_);
        main_queue_.push_back(std::move(fn));
    }
    const char byte = 1;
    [[maybe_unused]] const ssize_t n = write(pipe_[1], &byte, 1);
}

std::size_t Dispatcher::drain() {
    std::array<char, 64> sink{};
    while (read(pipe_[0], sink.data(), sink.size()) > 0) {}
    std::vector<std::function<void()>> batch;
    {
        const std::scoped_lock lock(main_mutex_);
        batch.swap(main_queue_);
    }
    for (const auto& fn : batch) fn();
    return batch.size();
}

void Dispatcher::shutdown() {
    for (auto& w : workers_) w.request_stop();
    {
        const std::scoped_lock lock(jobs_mutex_);
        jobs_.clear();
    }
    jobs_cv_.notify_all();
    workers_.clear(); // joins
}

void Dispatcher::worker(const std::stop_token& stop) {
    while (true) {
        Job job;
        {
            std::unique_lock lock(jobs_mutex_);
            if (!jobs_cv_.wait(lock, stop, [this] { return !jobs_.empty(); })) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        try {
            job(stop);
        } catch (const std::exception& e) {
            log::debug("job failed: {}", e.what());
        }
    }
}

} // namespace lv::app
