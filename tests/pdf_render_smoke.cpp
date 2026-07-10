#include "document/page_artifact.h"
#include "document_source/pdf/pdf_document.h"
#include "document_source/pdf/render_service.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void writeMinimalPdf(const std::string& path) {
    const std::vector<std::string> objects = {
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\nendobj\n",
    };

    std::ofstream out(path, std::ios::binary);
    out << "%PDF-1.4\n";

    std::vector<std::streamoff> offsets;
    offsets.reserve(objects.size());
    for (const auto& object : objects) {
        offsets.push_back(out.tellp());
        out << object;
    }

    const auto xref_offset = out.tellp();
    out << "xref\n0 " << objects.size() + 1 << "\n";
    out << "0000000000 65535 f \n";
    for (const auto offset : offsets) {
        char line[32];
        snprintf(line, sizeof(line), "%010lld 00000 n \n", static_cast<long long>(offset));
        out << line;
    }
    out << "trailer\n";
    out << "<< /Size " << objects.size() + 1 << " /Root 1 0 R >>\n";
    out << "startxref\n" << xref_offset << "\n%%EOF\n";
}

bool hasPngSignature(const std::string& path) {
    constexpr std::array<std::uint8_t, 8> expected = {
        0x89,
        0x50,
        0x4E,
        0x47,
        0x0D,
        0x0A,
        0x1A,
        0x0A,
    };

    std::array<char, expected.size()> actual = {};
    std::ifstream in(path, std::ios::binary);
    in.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    if (in.gcount() != static_cast<std::streamsize>(actual.size())) {
        return false;
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (static_cast<std::uint8_t>(actual[i]) != expected[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    const std::string pdf_path = "/tmp/document-intelligence-engine-render-smoke.pdf";
    const std::string output_root = "/tmp/document-intelligence-engine-render-smoke-output";
    writeMinimalPdf(pdf_path);

    doc_parser::pdf::PdfDocument document;

    if (!document.open(pdf_path)) {
        std::cerr << "failed to open generated PDF\n";
        return 1;
    }

    doc_parser::pdf::RenderService render_service;
    std::vector<doc_parser::document::PageArtifact> pages;
    if (!render_service.renderPages(document, {72, output_root, std::filesystem::path(output_root) / "pages"}, pages) ||
        pages.empty()) {
        std::cerr << "failed to render pages\n";
        return 1;
    }

    if (!hasPngSignature(pages.front().output_path.string())) {
        std::cerr << "rendered file is not a PNG\n";
        return 1;
    }

    return 0;
}
