#include "worker_job_lease.h"

#include <chrono>
#include <iostream>
#include <utility>

namespace doc_parser::platform {

WorkerJobLease::WorkerJobLease(std::string host, int port, RedisJobLease lease)
    : host_(std::move(host)), port_(port), lease_(std::move(lease)), thread_(&WorkerJobLease::run, this) {}

WorkerJobLease::~WorkerJobLease() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    thread_.join();
}

void WorkerJobLease::run() {
    try {
        RedisClient redis(host_, port_);
        std::unique_lock<std::mutex> lock(mutex_);
        while (!condition_.wait_for(lock, std::chrono::milliseconds(lease_.duration_ms / 3), [&] { return stop_; })) {
            lock.unlock();
            if (!redis.renewJob(lease_)) {
                lost_ = true;
                std::cerr << "job lease lost: message=" << lease_.message_id << " generation=" << lease_.generation
                          << '\n';
                return;
            }
            lock.lock();
        }
    } catch (const std::exception& error) {
        lost_ = true;
        std::cerr << "job lease renewal failed: message=" << lease_.message_id << " reason=" << error.what() << '\n';
    }
}

} // namespace doc_parser::platform
