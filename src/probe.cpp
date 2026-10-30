#include "adapters.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
void print(const std::wstring& s) {
    auto handle=GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode=0,written=0;
    if(GetConsoleMode(handle,&mode)){WriteConsoleW(handle,s.c_str(),static_cast<DWORD>(s.size()),&written,nullptr);return;}
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string utf8(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),utf8.data(),n,nullptr,nullptr);
    WriteFile(handle,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr);
}
int wmain(int argc,wchar_t** argv) {
    std::wostringstream out;
    if(argc>1 && std::wstring(argv[1])==L"--list") {
        for(const auto& d:battery::enumerate()) out<<battery::hex(d.vid)<<L":"<<battery::hex(d.pid)<<L"  "<<d.product
            <<L"  page="<<battery::hex(d.page)<<L" usage="<<d.usage<<L" in="<<d.input<<L" out="<<d.output<<L" feature="<<d.feature<<L'\n';
        print(out.str());return 0;
    }
    battery::Scanner scanner;auto s=scanner.scan();int failed=0;
    for(const auto& r:s.readings) {
        out<<r.name<<L"  "<<(r.percent?std::to_wstring(*r.percent)+L"%":L"--")<<L"  "<<battery::state_text(r);
        if(!r.detail.empty())out<<L"  "<<r.detail;
        out<<L'\n'; if(r.state!=battery::State::Online)++failed;
    }
    if(argc>1 && std::wstring(argv[1])==L"--debug")for(const auto& t:s.trace)out<<t<<L'\n';
    print(out.str());
    return failed?2:0;
}
