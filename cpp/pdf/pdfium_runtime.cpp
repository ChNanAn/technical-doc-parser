#include "pdf/pdfium_runtime.h"

namespace doc_parser::pdf::detail {

std::mutex& pdfiumMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace doc_parser::pdf::detail
