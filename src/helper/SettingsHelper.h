#pragma once

#include <array>
#include <sstream>
#include <string>

namespace SettingsHelper {
    inline auto EncodeColorMask(const std::array<float, 3>& mask) -> std::string
    {
        return std::to_string(mask[0]) + "," + std::to_string(mask[1]) + "," + std::to_string(mask[2]);
    }

    inline auto DecodeColorMask(const std::string& value, const std::array<float, 3>& fallback = { 0.0f, 0.0f, 0.0f }) -> std::array<float, 3>
    {
        std::array<float, 3> parsed = fallback;
        std::stringstream stream(value);
        std::string token;

        for (size_t i = 0; i < parsed.size(); ++i) {
            if (!std::getline(stream, token, ',')) {
                return fallback;
            }

            try {
                parsed[i] = std::stof(token);
            }
            catch (...) {
                return fallback;
            }
        }

        if (std::getline(stream, token, ',')) {
            return fallback;
        }

        return parsed;
    }
}
