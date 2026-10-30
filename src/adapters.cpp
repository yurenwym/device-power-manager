#include "adapters.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace battery {
namespace {
std::wstring dump(const Bytes& data) {
    std::wostringstream s; s << std::hex << std::setfill(L'0');
    for (auto b : data) s << std::setw(2) << static_cast<int>(b) << L' ';
    return s.str();
}
void record(Snapshot& s, const std::wstring& tag, const Bytes& data) {
    s.trace.push_back(tag + L": " + (data.empty() ? L"无有效回复" : dump(data)));
}
void assign(Reading& r, const protocol::Value& v) {
    r.state = State::Online; r.percent = v.percent; r.charging = v.charging; r.full = v.full;
    r.sampled = std::time(nullptr); r.detail.clear();
}
void failure(Reading& r, const Channel& c) {
    r.percent.reset(); r.charging.reset(); r.full=false; r.sampled=0;
    r.state = c.error() == ERROR_TIMEOUT || c.error() == ERROR_SUCCESS ? State::NoReply : State::Error;
    r.detail = r.state == State::NoReply ? L"接收器已连接，请唤醒设备后刷新" : error_text(c.error());
}
Reading preset(const wchar_t* id, const wchar_t* name, const wchar_t* source) {
    Reading r; r.id = id; r.name = name; r.source = source; r.detail = L"未检测到接收器"; return r;
}
void read_sora(const Interface& d, Reading& r, Snapshot& s) {
    Channel c(d); if (!c.valid()) { failure(r,c); return; }
    Bytes q(32); q[0] = 5; q[1] = 0x15; q[4] = 1;
    for (int retry = 0; retry < 2; ++retry) {
        if (!c.feature_set(q)) { failure(r,c); return; }
        Sleep(90);
        auto b = c.feature_get(5, 32); record(s, L"Sora V2 / 05 15", b);
        if (auto value = protocol::sora(b)) { assign(r,*value); return; }
    }
    failure(r,c);
}
void read_hyperx(const Interface& d, Reading& r, Snapshot& s) {
    Channel c(d); if (!c.valid()) { failure(r,c); return; }
    Bytes q(62); q[0] = 0x66; q[1] = 0x89;
    auto b = c.exchange(q, [](const Bytes& b) { return protocol::hyperx(b).has_value(); });
    record(s,L"Cloud III / 66 89",b);
    auto v = protocol::hyperx(b); if (!v) { failure(r,c); return; }
    assign(r,*v);
    q[1] = 0x8a;
    b = c.exchange(q, [](const Bytes& b) { return protocol::hyperx_charge(b).has_value(); }, 400);
    record(s,L"Cloud III / 66 8A",b);
    if (auto state = protocol::hyperx_charge(b)) { r.charging = *state == 1; r.full = *state == 2; }
}

// Read-only, descriptor-defined Battery Strength. No vendor command guessing.
std::optional<Reading> standard_feature(const Interface& d) {
    if (!d.feature) return {};
    Handle h(CreateFileW(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h) return {};
    PHIDP_PREPARSED_DATA pp = nullptr;
    if (!HidD_GetPreparsedData(h.get(), &pp)) return {};
    HIDP_CAPS caps{}; HidP_GetCaps(pp,&caps);
    std::vector<HIDP_VALUE_CAPS> values(caps.NumberFeatureValueCaps);
    USHORT count = static_cast<USHORT>(values.size());
    if (!count || HidP_GetValueCaps(HidP_Feature, values.data(), &count, pp) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(pp); return {};
    }
    std::optional<Reading> result;
    for (USHORT i=0; i<count; ++i) {
        auto& vc = values[i];
        if (vc.IsRange || vc.UsagePage != 6 || vc.NotRange.Usage != 0x20 ||
            vc.LogicalMax <= vc.LogicalMin || vc.LogicalMin < 0) continue;
        Channel c(d,false); auto b=c.feature_get(vc.ReportID,d.feature);
        ULONG raw = 0;
        if (b.empty() || HidP_GetUsageValue(HidP_Feature,6,vc.LinkCollection,0x20,&raw,pp,
            reinterpret_cast<PCHAR>(b.data()), static_cast<ULONG>(b.size())) != HIDP_STATUS_SUCCESS) continue;
        if (raw < static_cast<ULONG>(vc.LogicalMin) || raw > static_cast<ULONG>(vc.LogicalMax)) continue;
        Reading r = preset((L"standard:"+d.container).c_str(), d.product.empty()?L"标准 HID 电池设备":d.product.c_str(),L"标准 HID Battery Strength");
        r.transport=L"HID";
        assign(r,protocol::Value{static_cast<int>((static_cast<int64_t>(raw)-vc.LogicalMin)*100/(vc.LogicalMax-vc.LogicalMin)),{},false});
        result=r; break;
    }
    HidD_FreePreparsedData(pp); return result;
}
}
Snapshot Scanner::scan() {
    Snapshot s;
    s.readings = {preset(L"hyperx",L"飓风 3 Wireless",L"HyperX HID · 66 89 / 66 8A"),
        preset(L"sora",L"Sora V2",L"Ninjutso HID · Feature 05"),
        preset(L"z75s",L"迈从 Z75S",L"Compx HID · 13 4A"),
        preset(L"b98",L"iLovbee B98",L"Compx HID · 13 4A")};
    auto devices=enumerate();
    std::map<std::wstring,unsigned> counts;
    auto claim=[&](size_t index,const Interface& d)->size_t {
        auto model=s.readings[index].id.substr(0,s.readings[index].id.find(L':'));
        unsigned number=++counts[model];
        if(number>1){auto extra=s.readings[index];extra.percent.reset();extra.charging.reset();extra.sampled=0;extra.full=false;
            extra.name+=L"（"+std::to_wstring(number)+L"）";s.readings.push_back(extra);index=s.readings.size()-1;}
        s.readings[index].id=model+L":"+d.container;return index;
    };
    for (const auto& d:devices) {
        if (d.vid==0x1915 && (d.pid==0xae1c || d.pid==0xae11) && d.page==0xffa0 && d.feature>=32) {
            auto index=claim(1,d);read_sora(d,s.readings[index],s);
            if(d.pid==0xae11)s.readings[index].transport=L"USB";
        } else if (d.vid==0x03f0 && d.pid==0x0fcc && d.page==0xff13 && d.usage==1 && d.input==62 && d.output==62) {
            auto index=claim(0,d);read_hyperx(d,s.readings[index],s);
        } else if (d.vid==0x258a && d.pid==0x01bb && d.page==0xff02 && d.usage==2 && d.input==20 && d.output==20) {
            Channel c(d); std::wstring model;
            auto cached=bindings_.find(d.container); if (cached!=bindings_.end()) model=cached->second;
            Bytes identity;
            if (c.valid()) {
                identity=c.exchange(protocol::compx_query(5),[](const Bytes& b){return protocol::compx_identity(b).has_value();},400);
                record(s,L"键盘标识 / 13 05",identity);
                if (auto id=protocol::compx_identity(identity)) {
                    if (*id==std::array<uint8_t,6>{0x0a,0,0,0,0,0x09}) model=L"z75s";
                    else if (*id==std::array<uint8_t,6>{0x0a,0,0,0,0,0x10}) model=L"b98";
                    else { model.clear(); bindings_.erase(d.container); }
                    if (!model.empty()) bindings_[d.container]=model;
                }
            }
            size_t index=model==L"z75s"?2:model==L"b98"?3:s.readings.size();
            if (index==s.readings.size()) {
                Reading unknown=preset((L"unidentified:"+d.container).c_str(), L"未识别的键盘接收器",L"258A:01BB");
                unknown.state=identity.empty()?State::NoReply:State::Unsupported;
                unknown.detail=identity.empty()?L"请唤醒键盘后刷新以识别型号":L"设备标识不在适配表中";
                s.readings.push_back(unknown); continue;
            }
            index=claim(index,d);auto& r=s.readings[index];
            if (!c.valid()) {failure(r,c); continue;}
            auto b=c.exchange(protocol::compx_query(0x4a),[](const Bytes& b){return protocol::compx(b).has_value();},550);
            record(s,r.name+L" / 13 4A",b);
            if (auto v=protocol::compx(b)) assign(r,*v); else failure(r,c);
        } else if (d.feature && d.vid!=0x1915 && d.vid!=0x258a && d.vid!=0x03f0) {
            if (auto r=standard_feature(d)) {
                if (std::none_of(s.readings.begin(),s.readings.end(),[&](const Reading& x){return x.id==r->id;})) s.readings.push_back(*r);
            }
        }
    }
    return s;
}
std::wstring state_text(const Reading& r) {
    switch(r.state) {
    case State::Missing:return L"未连接";
    case State::NoReply:return L"休眠或离线";
    case State::Error:return L"读取失败";
    case State::Unsupported:return L"待适配";
    case State::Online:
        if (r.charging.value_or(false)) return L"充电中";
        if (r.full) return L"已充满";
        return r.charging.has_value()?L"使用中":L"在线 · 充电状态未知";
    }
    return L"未知";
}
}
