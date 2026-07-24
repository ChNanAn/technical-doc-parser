cmake_minimum_required(VERSION 3.20)
project(document_engine_embed_example LANGUAGES CXX)

find_package(DocumentIntelligenceEngine 0.1 CONFIG REQUIRED)

add_executable(embed_document embed_document.cpp)
target_link_libraries(embed_document PRIVATE DocumentIntelligenceEngine::engine)
target_compile_features(embed_document PRIVATE cxx_std_17)
