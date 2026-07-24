cmake_minimum_required(VERSION 3.20)
project(document_engine_install_consumer LANGUAGES CXX)

find_package(DocumentIntelligenceEngine 0.1 CONFIG REQUIRED)

add_executable(install_consumer ../../examples/embed_document.cpp)
target_link_libraries(install_consumer PRIVATE DocumentIntelligenceEngine::engine)
target_compile_features(install_consumer PRIVATE cxx_std_17)
