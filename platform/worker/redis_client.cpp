#include "redis_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

namespace doc_parser::platform {

struct RedisClient::Value {
    enum class Type {
        Null,
        String,
        Integer,
        Array,
        Error,
    };

    Type type = Type::Null;
    std::string string;
    long long integer = 0;
    std::vector<Value> array;
};

namespace {

std::string encodeCommand(const std::vector<std::string>& arguments) {
    std::string encoded = "*" + std::to_string(arguments.size()) + "\r\n";
    for (const std::string& argument : arguments) {
        encoded += "$" + std::to_string(argument.size()) + "\r\n" + argument + "\r\n";
    }
    return encoded;
}

void sendAll(int socket, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::send(socket, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (written <= 0) {
            throw std::runtime_error("failed to write Redis socket: " + std::string(std::strerror(errno)));
        }
        offset += static_cast<std::size_t>(written);
    }
}

} // namespace

RedisClient::RedisClient(std::string host, int port) : host_(std::move(host)), port_(port) { connectSocket(); }

RedisClient::~RedisClient() { closeSocket(); }

void RedisClient::connectSocket() {
    closeSocket();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const int result = getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &addresses);
    if (result != 0) {
        throw std::runtime_error("failed to resolve Redis host: " + std::string(gai_strerror(result)));
    }
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        socket_ = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_ >= 0) {
            const timeval timeout{10, 0};
            if (::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
                ::setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
                closeSocket();
                continue;
            }
        }
        if (socket_ >= 0 && ::connect(socket_, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        closeSocket();
    }
    freeaddrinfo(addresses);
    if (socket_ < 0) {
        throw std::runtime_error("failed to connect to Redis at " + host_ + ':' + std::to_string(port_));
    }
}

void RedisClient::closeSocket() {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    input_buffer_.clear();
}

std::string RedisClient::readLine() {
    while (true) {
        const std::size_t end = input_buffer_.find("\r\n");
        if (end != std::string::npos) {
            std::string line = input_buffer_.substr(0, end);
            input_buffer_.erase(0, end + 2);
            return line;
        }
        char buffer[4096];
        const ssize_t received = ::recv(socket_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("Redis connection closed while reading a line");
        }
        input_buffer_.append(buffer, static_cast<std::size_t>(received));
    }
}

std::string RedisClient::readExact(std::size_t size) {
    while (input_buffer_.size() < size) {
        char buffer[4096];
        const ssize_t received = ::recv(socket_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("Redis connection closed while reading a payload");
        }
        input_buffer_.append(buffer, static_cast<std::size_t>(received));
    }
    std::string result = input_buffer_.substr(0, size);
    input_buffer_.erase(0, size);
    return result;
}

RedisClient::Value RedisClient::readValue() {
    const std::string prefix = readExact(1);
    if (prefix == "+") {
        return {Value::Type::String, readLine(), 0, {}};
    }
    if (prefix == "-") {
        return {Value::Type::Error, readLine(), 0, {}};
    }
    if (prefix == ":") {
        return {Value::Type::Integer, {}, std::stoll(readLine()), {}};
    }
    if (prefix == "$") {
        const long long length = std::stoll(readLine());
        if (length < 0) {
            return {};
        }
        std::string data = readExact(static_cast<std::size_t>(length));
        if (readExact(2) != "\r\n") {
            throw std::runtime_error("invalid Redis bulk string terminator");
        }
        return {Value::Type::String, std::move(data), 0, {}};
    }
    if (prefix == "*") {
        const long long count = std::stoll(readLine());
        if (count < 0) {
            return {};
        }
        Value result;
        result.type = Value::Type::Array;
        result.array.reserve(static_cast<std::size_t>(count));
        for (long long index = 0; index < count; ++index) {
            result.array.push_back(readValue());
        }
        return result;
    }
    throw std::runtime_error("unsupported Redis RESP prefix: " + prefix);
}

RedisClient::Value RedisClient::command(const std::vector<std::string>& arguments) {
    try {
        if (socket_ < 0) {
            connectSocket();
        }
        sendAll(socket_, encodeCommand(arguments));
        Value response = readValue();
        if (response.type == Value::Type::Error) {
            throw std::runtime_error("Redis error: " + response.string);
        }
        return response;
    } catch (...) {
        closeSocket();
        throw;
    }
}

void RedisClient::ensureConsumerGroup(const std::string& stream, const std::string& group) {
    try {
        (void)command({"XGROUP", "CREATE", stream, group, "0", "MKSTREAM"});
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("BUSYGROUP") == std::string::npos) {
            throw;
        }
    }
}

std::optional<RedisStreamMessage> RedisClient::readGroup(const std::string& stream,
                                                         const std::string& group,
                                                         const std::string& consumer,
                                                         int block_milliseconds) {
    const Value response = command({"XREADGROUP",
                                    "GROUP",
                                    group,
                                    consumer,
                                    "COUNT",
                                    "1",
                                    "BLOCK",
                                    std::to_string(block_milliseconds),
                                    "STREAMS",
                                    stream,
                                    ">"});
    if (response.type == Value::Type::Null || response.array.empty()) {
        return std::nullopt;
    }
    const auto& stream_items = response.array.at(0).array.at(1).array;
    if (stream_items.empty()) {
        return std::nullopt;
    }
    const Value& item = stream_items.at(0);
    RedisStreamMessage message;
    message.id = item.array.at(0).string;
    const auto& fields = item.array.at(1).array;
    for (std::size_t index = 0; index + 1 < fields.size(); index += 2) {
        message.fields[fields[index].string] = fields[index + 1].string;
    }
    return message;
}

std::string RedisClient::addEvent(const std::string& stream, const std::string& json, std::size_t maximum_length) {
    if (maximum_length == 0) {
        throw std::invalid_argument("Redis stream maximum length must be positive");
    }
    return command({"XADD", stream, "MAXLEN", "~", std::to_string(maximum_length), "*", "event", json}).string;
}

void RedisClient::acknowledge(const std::string& stream, const std::string& group, const std::string& message_id) {
    (void)command({"XACK", stream, group, message_id});
}

void RedisClient::setHash(const std::string& key, const std::map<std::string, std::string>& values) {
    std::vector<std::string> command_arguments{"HSET", key};
    for (const auto& [field, value] : values) {
        command_arguments.push_back(field);
        command_arguments.push_back(value);
    }
    (void)command(command_arguments);
}

void RedisClient::expire(const std::string& key, int seconds) {
    (void)command({"EXPIRE", key, std::to_string(seconds)});
}

void RedisClient::publishEvent(const std::string& run_id,
                               const std::string& event,
                               const std::map<std::string, std::string>& state,
                               std::size_t run_maximum_length,
                               std::size_t platform_maximum_length,
                               int retention_seconds) {
    static const std::string script = R"(
        local expected = {'stream', 'stream', 'hash'}
        for i = 1, 3 do
            local kind = redis.call('TYPE', KEYS[i]).ok
            if kind ~= 'none' and kind ~= expected[i] then
                return redis.error_reply('WRONGTYPE event publication key ' .. KEYS[i])
            end
        end
        redis.call('XADD', KEYS[1], 'MAXLEN', '~', ARGV[2], '*', 'event', ARGV[1])
        redis.call('XADD', KEYS[2], 'MAXLEN', '~', ARGV[3], '*', 'event', ARGV[1])
        for i = 5, #ARGV, 2 do redis.call('HSET', KEYS[3], ARGV[i], ARGV[i + 1]) end
        redis.call('EXPIRE', KEYS[1], ARGV[4])
        redis.call('EXPIRE', KEYS[3], ARGV[4])
        return 1
    )";
    std::vector<std::string> arguments{
        "EVAL",
        script,
        "3",
        "run-events:" + run_id,
        "platform-events",
        "run:" + run_id,
        event,
        std::to_string(run_maximum_length),
        std::to_string(platform_maximum_length),
        std::to_string(retention_seconds),
    };
    for (const auto& [key, value] : state) {
        arguments.push_back(key);
        arguments.push_back(value);
    }
    (void)command(arguments);
}

} // namespace doc_parser::platform
