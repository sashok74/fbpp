#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace util {

enum class TraceLevel {
    info,
    warn,
    error
};

class TraceSink {
public:
    virtual ~TraceSink() = default;
    virtual void log(TraceLevel level,
                     std::string_view component,
                     std::string_view message) = 0;
};

void setTraceSink(TraceSink* sink);
TraceSink* getTraceSink();

void traceMessage(TraceLevel level,
                  std::string_view component,
                  std::string_view message);

template<typename Formatter>
inline void trace(TraceLevel level,
                  std::string_view component,
                  Formatter&& formatter) {
    if (auto* sink = getTraceSink()) {
        std::ostringstream oss;
        formatter(oss);
        sink->log(level, component, oss.str());
    }
}

} // namespace util

