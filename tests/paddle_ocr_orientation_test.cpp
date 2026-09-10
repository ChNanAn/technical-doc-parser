#include "image/page_image_cache.h"
#include "ocr/paddle_ocr_onnx_backend.h"
#include "pipeline/engine_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using doc_parser::document::BBox;
using doc_parser::ocr::OcrResult;
using doc_parser::ocr::PaddleOcrOnnxBackend;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("tdp_orientation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path), "cannot create test directory");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

BBox inverted(const BBox& box, const cv::Size& size) {
    return {size.width - box.x1, size.height - box.y1, size.width - box.x0, size.height - box.y0};
}

BBox rotatedBox(const BBox& box, const cv::Size& size, int degrees) {
    if (degrees == 90)
        return {size.height - box.y1, box.x0, size.height - box.y0, box.x1};
    if (degrees == 180)
        return inverted(box, size);
    if (degrees == 270)
        return {box.y0, size.width - box.x1, box.y1, size.width - box.x0};
    return box;
}

void sameBox(const BBox& actual, const BBox& expected) {
    require(std::abs(actual.x0 - expected.x0) < 1e-4 && std::abs(actual.y0 - expected.y0) < 1e-4 &&
                std::abs(actual.x1 - expected.x1) < 1e-4 && std::abs(actual.y1 - expected.y1) < 1e-4,
            "box was not preserved in source coordinates");
}

void samePage(const OcrResult& actual, const OcrResult& upright, const cv::Size& size, int degrees) {
    const auto expected_box = [&](const BBox& box) { return rotatedBox(box, size, degrees); };
    require(actual.page_text.has_text && actual.page_text.preferred_source == doc_parser::document::TextSource::Ocr,
            "missing OCR provenance");
    require(actual.page_text.lines.size() == upright.page_text.lines.size(), "line count changed");
    require(actual.regions.size() == upright.regions.size(), "region count changed");
    for (std::size_t i = 0; i < upright.page_text.lines.size(); ++i) {
        const auto& line = actual.page_text.lines[i];
        const auto& reference = upright.page_text.lines[i];
        require(line.text == reference.text, "recognized text/order differs from upright input");
        sameBox(line.bbox, expected_box(reference.bbox));
        require(line.spans.size() == reference.spans.size(), "span count changed");
        for (std::size_t j = 0; j < line.spans.size(); ++j) {
            require(line.spans[j].text == reference.spans[j].text, "span text changed");
            sameBox(line.spans[j].bbox, expected_box(reference.spans[j].bbox));
        }
    }
    for (std::size_t i = 0; i < upright.regions.size(); ++i) {
        require(actual.regions[i].text == upright.regions[i].text, "region text/order changed");
        sameBox(actual.regions[i].bbox, expected_box(upright.regions[i].bbox));
    }
}

void checkImage(const std::filesystem::path& input,
                const std::filesystem::path& root,
                const PaddleOcrOnnxBackend& enabled,
                const PaddleOcrOnnxBackend& disabled) {
    const cv::Mat image = cv::imread(input.string());
    require(!image.empty(), "cannot read " + input.string());
    auto cache = std::make_shared<doc_parser::image::PageImageCache>(64 * 1024 * 1024);
    doc_parser::document::PageArtifact page;
    page.page_index = 3;
    page.page_number = 4;
    page.width = image.cols;
    page.height = image.rows;
    page.output_path = input;
    page.image_cache = cache;

    OcrResult upright;
    require(enabled.recognize({page, 200}, upright), "upright recognition failed");
    require(upright.clockwise_correction_degrees == 0 && upright.page_text.lines.size() >= 2,
            "upright input unexpectedly rotated or sparse");
    OcrResult control;
    require(disabled.recognize({page, 200}, control), "upright control failed");
    samePage(control, upright, image.size(), 0);

    for (const int degrees : {90, 180, 270}) {
        cv::Mat rotated;
        cv::rotate(image,
                   rotated,
                   degrees == 90    ? cv::ROTATE_90_CLOCKWISE
                   : degrees == 180 ? cv::ROTATE_180
                                    : cv::ROTATE_90_COUNTERCLOCKWISE);
        const auto rotated_path = root / (input.stem().string() + "-" + std::to_string(degrees) + ".png");
        require(cv::imwrite(rotated_path.string(), rotated), "cannot write rotated image");
        auto rotated_page = page;
        rotated_page.width = rotated.cols;
        rotated_page.height = rotated.rows;
        rotated_page.output_path = rotated_path;
        rotated_page.page_index = 7;
        rotated_page.page_number = 8;
        require(disabled.recognize({rotated_page, 200}, control), "rotated control failed");
        require(control.clockwise_correction_degrees == 0, "disabled recovery still rotated input");
        OcrResult recovered;
        require(enabled.recognize({rotated_page, 200}, recovered), "rotated recognition failed");
        require(recovered.clockwise_correction_degrees == 360 - degrees,
                "did not recover rotation " + std::to_string(degrees));
        require(recovered.page_text.page_index == 7 && recovered.page_text.page_number == 8, "page identity changed");
        samePage(recovered, upright, image.size(), degrees);

        // Region-only requests do not infer page orientation; retain their 180-degree recovery coverage.
        if (degrees == 180) {
            std::vector<BBox> boxes;
            for (const auto& region : upright.regions) {
                boxes.push_back(region.bbox);
            }
            // Caller order can differ from reading order, and invalid regions must keep their slots.
            std::reverse(boxes.begin(), boxes.end());
            boxes.insert(boxes.begin() + 1, BBox{0, 0, 0, 0});
            std::vector<BBox> upside_boxes;
            for (const auto& box : boxes) {
                upside_boxes.push_back(inverted(box, image.size()));
            }
            doc_parser::ocr::OcrRegionRecognitionResult upright_regions;
            doc_parser::ocr::OcrRegionRecognitionResult recovered_regions;
            require(enabled.recognizeRegions({page, 200, boxes}, upright_regions), "upright region recognition failed");
            require(enabled.recognizeRegions({rotated_page, 200, upside_boxes}, recovered_regions),
                    "rotated region recognition failed");
            require(upright_regions.regions.size() == boxes.size() && recovered_regions.regions.size() == boxes.size(),
                    "region request slots changed");
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                sameBox(recovered_regions.regions[i].bbox, upside_boxes[i]);
                require(recovered_regions.regions[i].text == upright_regions.regions[i].text,
                        "supplied region text/order was not recovered");
            }
            require(recovered_regions.regions[1].text.empty(), "invalid region unexpectedly recognized");
        }
        require(cv::norm(cache->read(input), image, cv::NORM_INF) == 0 &&
                    cv::norm(cache->read(rotated_path), rotated, cv::NORM_INF) == 0,
                "shared cached pixels changed");
        require(cv::norm(cv::imread(rotated_path.string()), rotated, cv::NORM_INF) == 0, "source image file changed");
        std::cout << input.filename() << " degrees=" << degrees << ": text, order, source boxes and cache verified\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: paddle_ocr_orientation_test IMAGE IMAGE [IMAGE...]\n";
        return 2;
    }
    try {
        auto config = doc_parser::pipeline::defaultEngineConfig().paddle_ocr;
        const PaddleOcrOnnxBackend enabled(config);
        config.recover_upside_down = false;
        const PaddleOcrOnnxBackend disabled(config);
        require(enabled.isAvailable() && disabled.isAvailable(), "default PaddleOCR models are unavailable");
        const TemporaryDirectory directory;
        for (int i = 1; i < argc; ++i) {
            checkImage(argv[i], directory.path, enabled, disabled);
        }
    } catch (const std::exception& error) {
        std::cerr << "orientation regression failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
