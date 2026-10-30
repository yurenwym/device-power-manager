#include "../src/hid.hpp"
#include <iostream>
using namespace battery;
int main(){
    auto path=L"\\\\.\\pipe\\WirelessBatteryIoTest"+std::to_wstring(GetCurrentProcessId());
    Handle server(CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,512,512,0,nullptr));
    if(!server)return 1;
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event.get();
    ConnectNamedPipe(server.get(),&ov);
    Interface iface;iface.path=path;iface.input=20;iface.output=20;
    Channel client(iface);if(!client.valid())return 2;
    WaitForSingleObject(event.get(),1000);
    DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    auto started=GetTickCount64();
    for(int i=0;i<40;++i){if(!client.input(10).empty()||client.error()!=ERROR_TIMEOUT)return 3;}
    if(GetTickCount64()-started>6000)return 4;
    GetProcessHandleCount(GetCurrentProcess(),&after);if(after>before+1)return 5;
    // A fresh read still succeeds after the timed-out requests were retired.
    Bytes bytes(20,42);OVERLAPPED send{};send.hEvent=event.get();ResetEvent(event.get());DWORD n=0;
    BOOL ok=WriteFile(server.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&n,&send);
    if(!ok&&GetLastError()!=ERROR_IO_PENDING)return 6;
    auto reply=client.input(500);if(reply!=bytes)return 7;
    if(!GetOverlappedResult(server.get(),&send,&n,TRUE))return 8;
    std::cout<<"40 cancelled reads: bounded waits, stable handles, next read succeeds\n";
}
