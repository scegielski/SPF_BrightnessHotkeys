#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace Brightness {
// Plugin guardrails, not a claim about the game's accepted cvar range.
constexpr int64_t Scale = 1000000, Min = -2000000, Max = 3000000;
inline bool Units(double value, int64_t& result, bool step = false) {
    if (!std::isfinite(value) || value < (step ? 0.000001 : -2.0) || value > (step ? 5.0 : 3.0)) return false;
    result = std::llround(value * Scale);
    return !step || result > 0;
}
inline std::string Decimal(int64_t units) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << static_cast<double>(units) / Scale;
    return out.str();
}
// Recognize one complete literal assignment only; never infer tracking from compound commands.
inline bool Assignment(const std::string& command, int64_t& result) {
    std::istringstream in(command); in.imbue(std::locale::classic());
    std::string name; in >> name;
    if (name == "uset") in >> name;
    if (name != "r_sdr_display_gray_offset") return false;
    in >> std::ws;
    bool quoted = in.peek() == '"'; if (quoted) in.get();
    double value; if (!(in >> value)) return false;
    if (quoted && in.get() != '"') return false;
    in >> std::ws;
    return in.eof() && Units(value, result);
}
struct State {
    int64_t value = 0, step = 100000;
    bool ready = false;
    int64_t Next(int direction) const { return std::clamp(value + direction * step, Min, Max); }
};
}
