#include "worker_stage_observer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class RecordingEventWriter final : public doc_parser::platform::IRedisEventWriter {
public:
    struct StreamWrite {
        std::string stream;
        std::string json;
        std::size_t maximum_length = 0;
    };

    std::string addEvent(const std::string& stream, const std::string& json, std::size_t maximum_length) override {
        stream_writes.push_back({stream, json, maximum_length});
        return std::to_string(stream_writes.size()) + "-0";
    }

    void setHash(const std::string& key, const std::map<std::string, std::string>& values) override {
        hash_writes.emplace_back(key, values);
    }

    void expire(const std::string& key, int seconds) override { expirations.emplace_back(key, seconds); }

    std::vector<StreamWrite> stream_writes;
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> hash_writes;
    std::vector<std::pair<std::string, int>> expirations;
};

} // namespace

TEST(WorkerStageObserverTest, RefreshesRunStateAndEventStreamRetentionForEveryEvent) {
    RecordingEventWriter writer;
    const std::filesystem::path run_directory = std::filesystem::temp_directory_path() / "tdp_worker_stage_observer_"
                                                                                         "test";
    std::filesystem::remove_all(run_directory);
    doc_parser::platform::WorkerStageObserver observer(
        writer, "job_1", "run_1", "attempt_1", run_directory, 2'000, 100'000, 600);

    observer.publishJobEvent("job_started");
    observer.onStageStarted({"layout", "doclaynet", 1});

    ASSERT_EQ(writer.stream_writes.size(), 4U);
    EXPECT_EQ(writer.stream_writes[0].stream, "run-events:run_1");
    EXPECT_EQ(writer.stream_writes[0].maximum_length, 2'000U);
    EXPECT_EQ(writer.stream_writes[1].stream, "platform-events");
    EXPECT_EQ(writer.stream_writes[1].maximum_length, 100'000U);
    EXPECT_EQ(writer.stream_writes[2].stream, "run-events:run_1");
    EXPECT_EQ(writer.stream_writes[3].stream, "platform-events");

    ASSERT_EQ(writer.expirations.size(), 4U);
    EXPECT_EQ(writer.expirations[0], std::make_pair(std::string("run-events:run_1"), 600));
    EXPECT_EQ(writer.expirations[1], std::make_pair(std::string("run:run_1"), 600));
    EXPECT_EQ(writer.expirations[2], std::make_pair(std::string("run-events:run_1"), 600));
    EXPECT_EQ(writer.expirations[3], std::make_pair(std::string("run:run_1"), 600));
    ASSERT_GE(writer.hash_writes.size(), 3U);
    EXPECT_EQ(writer.hash_writes[0].first, "run:run_1");
    EXPECT_TRUE(writer.hash_writes[0].second.count("last_event"));
    EXPECT_EQ(writer.hash_writes.back().second.at("stage"), "layout");

    std::filesystem::remove_all(run_directory);
}

TEST(WorkerStageObserverTest, RejectsNonPositiveRetention) {
    RecordingEventWriter writer;
    EXPECT_THROW(doc_parser::platform::WorkerStageObserver(
                     writer, "job_1", "run_1", "attempt_1", std::filesystem::temp_directory_path(), 2'000, 100'000, 0),
                 std::invalid_argument);
}
