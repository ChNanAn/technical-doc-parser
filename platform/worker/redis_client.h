#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc_parser::platform {

struct RedisStreamMessage {
    std::string id;
    std::map<std::string, std::string> fields;
};

class RedisClient {
public:
    RedisClient(std::string host, int port);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    void ensureConsumerGroup(const std::string& stream, const std::string& group);
    std::optional<RedisStreamMessage>
    readGroup(const std::string& stream, const std::string& group, const std::string& consumer, int block_milliseconds);
    std::string addEvent(const std::string& stream, const std::string& json);
    void acknowledge(const std::string& stream, const std::string& group, const std::string& message_id);
    void setHash(const std::string& key, const std::map<std::string, std::string>& values);
    void expire(const std::string& key, int seconds);

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
};

} // namespace doc_parser::platform
