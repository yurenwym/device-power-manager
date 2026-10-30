#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "protocols.hpp"

namespace battery {
using Bytes = protocol::Bytes;
class Handle {
    HANDLE h_ = INVALID_HANDLE_VALUE;
public:
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : h_(h) {}
    ~Handle() { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
};
struct Interface {
    std::wstring path, container, product;
    USHORT vid = 0, pid = 0, page = 0, usage = 0, input = 0, output = 0, feature = 0;
};
std::vector<Interface> enumerate();
std::wstring error_text(DWORD code);
std::wstring hex(USHORT value);

struct HidOperation;
class Channel {
    Interface iface_;
    std::shared_ptr<Handle> io_;
    DWORD error_ = ERROR_SUCCESS;
    bool finish(BOOL started, const std::shared_ptr<HidOperation>& op, DWORD timeout);
    std::shared_ptr<HidOperation> operation(Bytes bytes);
public:
    explicit Channel(const Interface& iface, bool write = true);
    bool valid() const { return io_ && static_cast<bool>(*io_); }
    DWORD error() const { return error_; }
    bool output(const Bytes& bytes, DWORD timeout = 450);
    bool feature_set(const Bytes& bytes, DWORD timeout = 450);
    Bytes feature_get(uint8_t report, size_t length, DWORD timeout = 450);
    Bytes input(DWORD timeout = 450);
    void drain();
    Bytes exchange(const Bytes& request, const std::function<bool(const Bytes&)>& accept, DWORD timeout = 650);
};
}
