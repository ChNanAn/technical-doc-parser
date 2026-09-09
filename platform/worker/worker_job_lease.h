#pragma once

#include "redis_client.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace doc_parser::platform {

// A separate Redis connection renews ownership during long model calls. A failed
// renewal never resurrects an expired lease; event publication also checks it in Redis.
class WorkerJobLease {
public:
    WorkerJobLease(std::string host, int port, RedisJobLease lease);
    ~WorkerJobLease();
    WorkerJobLease(const WorkerJobLease&) = delete;
    WorkerJobLease& operator=(const WorkerJobLease&) = delete;
    bool lost() const { return lost_; }

private:
    void run();
    std::string host_;
    int port_;
    RedisJobLease lease_;
    std::atomic<bool> lost_{false};
    bool stop_ = false;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
};

} // namespace doc_parser::platform
