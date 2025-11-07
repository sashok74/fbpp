#include <fbpp_util/trace.h>

#include <atomic>

namespace util {
namespace {
std::atomic<TraceSink*> g_traceSink{nullptr};
} // namespace

void setTraceSink(TraceSink* sink) {
    g_traceSink.store(sink, std::memory_order_relaxed);
}

TraceSink* getTraceSink() {
    return g_traceSink.load(std::memory_order_relaxed);
}

void traceMessage(TraceLevel level,
                  std::string_view component,
                  std::string_view message) {
    if (auto* sink = getTraceSink()) {
        sink->log(level, component, message);
    }
}

} // namespace util

