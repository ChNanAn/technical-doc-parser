#pragma once

#include <mutex>

namespace doc_parser::pdf::detail {

// PDFium exposes process-wide state and should be treated as a guarded native
// dependency. Keep direct synchronization here so public PDF APIs can evolve
// without leaking threading policy to callers.
std::mutex& pdfiumMutex();

}  // namespace doc_parser::pdf::detail
