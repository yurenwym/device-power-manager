#pragma once
#include "hid.hpp"
#include <map>
#include <optional>
#include <ctime>

namespace battery {
enum class State { Online, Missing, NoReply, Error, Unsupported };
struct Reading {
    std::wstring id, name, transport = L"2.4G", source, detail;
    State state = State::Missing;
    std::optional<int> percent;
    std::optional<bool> charging;
    bool full = false;
    std::time_t sampled = 0;
};
struct Snapshot { std::vector<Reading> readings; std::vector<std::wstring> trace; };
using Bindings = std::map<std::wstring, std::wstring>;
class Scanner {
    Bindings bindings_;
public:
    explicit Scanner(Bindings bindings = {}) : bindings_(std::move(bindings)) {}
    Snapshot scan();
    const Bindings& bindings() const { return bindings_; }
};
std::wstring state_text(const Reading& r);
}
