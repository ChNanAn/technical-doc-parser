#include <document_intelligence_engine/document_engine.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: embed_document <input.pdf> <output-directory>\n";
        return 1;
    }

    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends = {"pdf", "noop", "text", "text", {}};
    doc_parser::pipeline::DocumentEngine engine(config);
    if (!engine.isReady()) {
        std::cerr << engine.initializationStatus().message() << '\n';
        return 2;
    }

    doc_parser::pipeline::DocumentParseOptions options;
    options.input_path = argv[1];
    options.output_directory = argv[2];
    const doc_parser::pipeline::ParseResult result = engine.parse(options);
    if (!result.ok()) {
        std::cerr << result.status.message() << '\n';
        return 2;
    }

    std::cout << "parsed " << result.document.blocks.size() << " blocks\n";
    return 0;
}
