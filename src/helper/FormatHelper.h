#pragma once

#include <sstream>
#include <string>
#include <utility>

namespace format_helper {
    inline auto ReplaceNextPlaceholder(std::string& format, const std::string& replacement) -> void {
        const auto pos = format.find("{}");
        if (pos != std::string::npos) {
            format.replace(pos, 2, replacement);
        }
    }

    template <typename T>
    auto ToString(const T& value) -> std::string {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }

    inline auto Format(std::string format) -> std::string {
        return format;
    }

    template <typename Arg, typename... Args>
    auto Format(std::string format, const Arg& arg, const Args&... args) -> std::string {
        ReplaceNextPlaceholder(format, ToString(arg));
        return Format(std::move(format), args...);
    }
}
