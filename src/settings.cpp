#include "settings.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>

namespace battery {
namespace {
constexpr wchar_t link_name[]=L"设备电源管理.lnk";
constexpr wchar_t approved[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";
std::wstring folder(REFKNOWNFOLDERID id) {
    PWSTR s=nullptr; if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&s))) return {};
    std::wstring r=s;CoTaskMemFree(s);return r;
}
std::wstring ini() { auto path=data_folder();return path.empty()?L"":path+L"\\settings.ini"; }
std::wstring startup_link() {auto path=folder(FOLDERID_Startup);return path.empty()?L"":path+L"\\"+link_name;}
bool link_is_ours(const std::wstring& path) {
    IShellLinkW* link=nullptr; bool ours=false;
    if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))) {
        IPersistFile* pf=nullptr;
        if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
            if(SUCCEEDED(pf->Load(path.c_str(),STGM_READ))) {
                wchar_t target[32768]{};
                if(SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH))) ours=_wcsicmp(target,executable().c_str())==0;
            } pf->Release();
        } link->Release();
    } return ours;
}
}
std::wstring executable() {std::wstring p(32768,L'\0');auto n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));p.resize(n);return p;}
std::wstring data_folder() {
    auto path=folder(FOLDERID_LocalAppData);if(path.empty())return {};
    path+=L"\\WirelessBattery";
    if(!CreateDirectoryW(path.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return {};
    return path;
}
Settings load_settings() {
    Settings s;auto path=ini();if(path.empty())return s;
    s.interval=GetPrivateProfileIntW(L"Settings",L"Interval",60,path.c_str());
    if(s.interval!=30 && s.interval!=60 && s.interval!=120 && s.interval!=300 && s.interval!=600 && s.interval!=1800)s.interval=60;
    s.threshold=GetPrivateProfileIntW(L"Settings",L"Threshold",20,path.c_str());
    if(s.threshold!=10 && s.threshold!=15 && s.threshold!=20 && s.threshold!=30)s.threshold=20;
    s.notify=GetPrivateProfileIntW(L"Settings",L"Notify",1,path.c_str())!=0;
    std::vector<wchar_t> data(32768);
    GetPrivateProfileSectionW(L"Bindings",data.data(),static_cast<DWORD>(data.size()),path.c_str());
    for(const wchar_t* p=data.data();*p;p+=wcslen(p)+1) {
        std::wstring entry=p;auto split=entry.find(L'=');if(split==std::wstring::npos)continue;
        auto value=entry.substr(split+1);if(value==L"z75s"||value==L"b98")s.bindings[entry.substr(0,split)]=value;
    }return s;
}
bool save_settings(const Settings& s) {
    auto path=ini();if(path.empty())return false;
    bool ok=WritePrivateProfileStringW(L"Settings",L"Interval",std::to_wstring(s.interval).c_str(),path.c_str())!=FALSE;
    ok=WritePrivateProfileStringW(L"Settings",L"Threshold",std::to_wstring(s.threshold).c_str(),path.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"Settings",L"Notify",s.notify?L"1":L"0",path.c_str()) && ok;
    // Replace this application's complete binding section to remove obsolete mappings.
    std::vector<wchar_t> entries;
    for(const auto& [key,value]:s.bindings){auto line=key+L"="+value;entries.insert(entries.end(),line.begin(),line.end());entries.push_back(0);}
    entries.push_back(0);if(entries.size()==1)entries.push_back(0);
    return WritePrivateProfileSectionW(L"Bindings",entries.data(),path.c_str()) && ok;
}
bool startup_enabled() {
    auto path=startup_link();if(path.empty() || GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES || !link_is_ours(path))return false;
    BYTE state[12]{};DWORD size=sizeof(state);
    if(RegGetValueW(HKEY_CURRENT_USER,approved,link_name,RRF_RT_REG_BINARY,nullptr,state,&size)==ERROR_SUCCESS && size>=1 && state[0]==3)return false;
    return true;
}
HRESULT set_startup(bool enabled) {
    auto path=startup_link();if(path.empty())return E_FAIL;
    if(!enabled) {
        if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) {
            if(!link_is_ours(path))return E_ACCESSDENIED;
            if(!DeleteFileW(path.c_str()))return HRESULT_FROM_WIN32(GetLastError());
        }return S_OK;
    }
    IShellLinkW* link=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
    if(FAILED(hr))return hr;
    auto exe=executable();auto dir=exe.substr(0,exe.find_last_of(L"\\/"));
    hr=link->SetPath(exe.c_str());
    if(SUCCEEDED(hr))hr=link->SetArguments(L"--tray");
    if(SUCCEEDED(hr))hr=link->SetWorkingDirectory(dir.c_str());
    if(SUCCEEDED(hr))hr=link->SetDescription(L"设备电源管理");
    IPersistFile* pf=nullptr;
    if(SUCCEEDED(hr))hr=link->QueryInterface(IID_PPV_ARGS(&pf));
    if(pf){if(SUCCEEDED(hr))hr=pf->Save(path.c_str(),TRUE);pf->Release();}
    link->Release();
    if(SUCCEEDED(hr)) {
        HKEY key=nullptr;
        if(RegOpenKeyExW(HKEY_CURRENT_USER,approved,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){RegDeleteValueW(key,link_name);RegCloseKey(key);}
    }
    return hr;
}
}
