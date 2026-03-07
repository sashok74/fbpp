#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace fbpp::util {

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

/**
 * Install a process-wide trace sink for fbpp runtime diagnostics.
 *
 * The sink pointer is published atomically and observed from all runtime code.
 * The sink implementation itself must be safe for concurrent calls if multiple
 * fbpp objects are used from different threads.
 */
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

} // namespace fbpp::util
