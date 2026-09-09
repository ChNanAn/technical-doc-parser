#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc_parser::platform {

struct RedisStreamMessage {
    std::string id;
    std::map<std::string, std::string> fields;
};

struct RedisJobLease {
    std::string key;
    std::string stream;
    std::string group;
    std::string message_id;
    std::string consumer;
    std::int64_t generation = 0;
    std::int64_t sequence = 0;
    int duration_ms = 0;
};

class IRedisEventWriter {
public:
    virtual ~IRedisEventWriter() = default;

    virtual std::string addEvent(const std::string& stream, const std::string& json, std::size_t maximum_length) = 0;
    virtual void setHash(const std::string& key, const std::map<std::string, std::string>& values) = 0;
    virtual void expire(const std::string& key, int seconds) = 0;
    virtual void publishEvent(const std::string& run_id,
                              const std::string& event,
                              const std::map<std::string, std::string>& state,
                              std::size_t run_maximum_length,
                              std::size_t platform_maximum_length,
                              int retention_seconds) = 0;
};

class RedisClient final : public IRedisEventWriter {
public:
    RedisClient(std::string host, int port);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    void ensureConsumerGroup(const std::string& stream, const std::string& group);
    std::optional<RedisStreamMessage>
    readGroup(const std::string& stream, const std::string& group, const std::string& consumer, int block_milliseconds);
    std::optional<RedisStreamMessage> reclaimExpired(const std::string& stream,
                                                     const std::string& group,
                                                     const std::string& consumer,
                                                     int idle_ms,
                                                     std::string& cursor);
    std::optional<RedisJobLease> acquireJob(const std::string& stream,
                                            const std::string& group,
                                            const std::string& consumer,
                                            const RedisStreamMessage& message,
                                            int lease_ms);
    bool renewJob(const RedisJobLease& lease);
    void clearJob() { lease_.reset(); }
    std::string addEvent(const std::string& stream, const std::string& json, std::size_t maximum_length) override;
    void acknowledge(const std::string& stream, const std::string& group, const std::string& message_id);
    void setHash(const std::string& key, const std::map<std::string, std::string>& values) override;
    void expire(const std::string& key, int seconds) override;
    void publishEvent(const std::string& run_id,
                      const std::string& event,
                      const std::map<std::string, std::string>& state,
                      std::size_t run_maximum_length,
                      std::size_t platform_maximum_length,
                      int retention_seconds) override;

private:
    struct Value;

    Value command(const std::vector<std::string>& arguments);
    void connectSocket();
    void closeSocket();
    std::string readLine();
    std::string readExact(std::size_t size);
    Value readValue();

    std::string host_;
    int port_ = 6379;
    int socket_ = -1;
    std::string input_buffer_;
    std::optional<RedisJobLease> lease_;
};

} // namespace doc_parser::platform
