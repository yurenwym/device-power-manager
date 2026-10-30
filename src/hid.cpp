#include "hid.hpp"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidclass.h>
#include <objbase.h>
#include <memory>
#include <mutex>
#include <algorithm>

namespace battery {
std::wstring hex(USHORT v) { wchar_t s[8]; swprintf_s(s, L"%04X", v); return s; }
std::wstring error_text(DWORD c) {
    wchar_t* p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, c, 0, reinterpret_cast<LPWSTR>(&p), 0, nullptr);
    std::wstring s = p ? p : L"未知错误";
    if (p) LocalFree(p);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
    return s + L" (" + std::to_wstring(c) + L")";
}
std::vector<Interface> enumerate() {
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return {};
    std::vector<Interface> result;
    SP_DEVICE_INTERFACE_DATA di{sizeof(di)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &di); ++i) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &di, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<uint8_t> storage(required);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA info{sizeof(info)};
        if (!SetupDiGetDeviceInterfaceDetailW(set, &di, detail, required, nullptr, &info)) continue;
        Interface x; x.path = detail->DevicePath;
        Handle h(CreateFileW(x.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h) continue;
        HIDD_ATTRIBUTES attr{sizeof(attr)};
        if (!HidD_GetAttributes(h.get(), &attr)) continue;
        x.vid = attr.VendorID; x.pid = attr.ProductID;
        wchar_t product[256]{}; if (HidD_GetProductString(h.get(), product, sizeof(product))) x.product = product;
        PHIDP_PREPARSED_DATA pp = nullptr;
        if (HidD_GetPreparsedData(h.get(), &pp)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                x.page = caps.UsagePage; x.usage = caps.Usage;
                x.input = caps.InputReportByteLength; x.output = caps.OutputReportByteLength;
                x.feature = caps.FeatureReportByteLength;
            }
            HidD_FreePreparsedData(pp);
        }
        GUID container{}; DEVPROPTYPE type = 0;
        if (SetupDiGetDevicePropertyW(set, &info, &DEVPKEY_Device_ContainerId, &type,
                reinterpret_cast<PBYTE>(&container), sizeof(container), nullptr, 0) && type == DEVPROP_TYPE_GUID) {
            wchar_t text[40]{}; StringFromGUID2(container, text, 40); x.container = text;
        }
        if (x.container.empty()) {
            wchar_t instance[MAX_DEVICE_ID_LEN]{};
            SetupDiGetDeviceInstanceIdW(set, &info, instance, MAX_DEVICE_ID_LEN, nullptr);
            x.container = instance;
        }
        result.push_back(std::move(x));
    }
    SetupDiDestroyDeviceInfoList(set); return result;
}
struct HidOperation {
    std::shared_ptr<Handle> io;
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    OVERLAPPED ov{};
    Bytes data;
    DWORD count=0;
    std::wstring path;
    HidOperation(std::shared_ptr<Handle> h,Bytes b,std::wstring p):io(std::move(h)),data(std::move(b)),path(std::move(p)){ov.hEvent=event.get();}
};
namespace {
// A broken driver may ignore cancellation. Keep at most 16 requests alive,
// including their handles, events, buffers and OVERLAPPED, until completion.
// This process-lifetime storage is intentionally not destroyed during CRT exit:
// the OS reclaims it after all process I/O has been cancelled.
struct PendingIo{std::mutex mutex;std::vector<std::shared_ptr<HidOperation>> requests;};
PendingIo& pending_io(){static auto* pending=new PendingIo;return *pending;}
bool can_issue(const std::wstring& path){
    auto& p=pending_io();std::lock_guard<std::mutex> lock(p.mutex);
    p.requests.erase(std::remove_if(p.requests.begin(),p.requests.end(),[](const auto& op){return WaitForSingleObject(op->event.get(),0)==WAIT_OBJECT_0;}),p.requests.end());
    return p.requests.size()<16 && std::none_of(p.requests.begin(),p.requests.end(),[&](const auto& op){return op->path==path;});
}
}
Channel::Channel(const Interface& iface, bool write) : iface_(iface), io_(std::make_shared<Handle>(CreateFileW(iface.path.c_str(),
    write ? GENERIC_READ | GENERIC_WRITE : 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr))) { if (!valid()) error_ = GetLastError(); }
std::shared_ptr<HidOperation> Channel::operation(Bytes bytes){
    if(!valid())return {};
    if(!can_issue(iface_.path)){error_=ERROR_BUSY;return {};}
    auto op=std::make_shared<HidOperation>(io_,std::move(bytes),iface_.path);
    if(!op->event){error_=GetLastError();return {};}
    return op;
}
bool Channel::finish(BOOL started, const std::shared_ptr<HidOperation>& op, DWORD timeout) {
    if (!started && GetLastError() != ERROR_IO_PENDING) { error_ = GetLastError(); return false; }
    if (!started && WaitForSingleObject(op->event.get(), timeout) != WAIT_OBJECT_0) {
        CancelIoEx(io_->get(), &op->ov);
        if(WaitForSingleObject(op->event.get(),100)!=WAIT_OBJECT_0){
            auto& p=pending_io();std::lock_guard<std::mutex> lock(p.mutex);p.requests.push_back(op);
        }else GetOverlappedResult(io_->get(),&op->ov,&op->count,FALSE);
        error_ = ERROR_TIMEOUT; return false;
    }
    if (!GetOverlappedResult(io_->get(), &op->ov, &op->count, FALSE)) { error_ = GetLastError(); return false; }
    error_ = ERROR_SUCCESS; return true;
}
bool Channel::output(const Bytes& bytes, DWORD timeout) {
    if (!valid() || bytes.empty() || bytes.size() != iface_.output) { error_ = ERROR_INVALID_PARAMETER; return false; }
    auto op=operation(bytes);if(!op)return false;
    if (finish(DeviceIoControl(io_->get(), IOCTL_HID_SET_OUTPUT_REPORT, op->data.data(),
            static_cast<DWORD>(op->data.size()), nullptr, 0, &op->count, &op->ov), op, timeout)) return true;
    if (error_ == ERROR_TIMEOUT) return false;
    op=operation(bytes);if(!op)return false;
    return finish(WriteFile(io_->get(),op->data.data(),static_cast<DWORD>(op->data.size()),&op->count,&op->ov),op,timeout) && op->count==bytes.size();
}
bool Channel::feature_set(const Bytes& bytes, DWORD timeout) {
    if (!valid() || bytes.empty()) return false;
    auto op=operation(bytes);if(!op)return false;
    return finish(DeviceIoControl(io_->get(), IOCTL_HID_SET_FEATURE,op->data.data(),
        static_cast<DWORD>(op->data.size()),nullptr,0,&op->count,&op->ov),op,timeout);
}
Bytes Channel::feature_get(uint8_t report, size_t length, DWORD timeout) {
    if (!valid() || !length || length > 65535) return {};
    Bytes data(length); data[0] = report;
    auto op=operation(std::move(data));if(!op)return {};
    if (!finish(DeviceIoControl(io_->get(),IOCTL_HID_GET_FEATURE,nullptr,0,op->data.data(),
            static_cast<DWORD>(op->data.size()),&op->count,&op->ov),op,timeout)) return {};
    if (op->count < op->data.size()) op->data.resize(op->count);
    return std::move(op->data);
}
Bytes Channel::input(DWORD timeout) {
    if (!valid() || !iface_.input) return {};
    auto op=operation(Bytes(iface_.input));if(!op)return {};
    if(!finish(ReadFile(io_->get(),op->data.data(),static_cast<DWORD>(op->data.size()),&op->count,&op->ov),op,timeout))return {};
    op->data.resize(op->count);return std::move(op->data);
}
void Channel::drain() { for (int n = 0; n < 12 && !input(1).empty(); ++n) {} }
Bytes Channel::exchange(const Bytes& request, const std::function<bool(const Bytes&)>& accept, DWORD timeout) {
    drain(); if (!output(request)) return {};
    ULONGLONG end = GetTickCount64() + timeout;
    for (;;) {
        auto now = GetTickCount64(); if (now >= end) break;
        auto b = input(static_cast<DWORD>(end - now));
        if (b.empty()) break;
        if (accept(b)) return b;
    }
    return {};
}
}
