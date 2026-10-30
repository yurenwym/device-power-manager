#include "settings.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <dbt.h>
#include <thread>
#include <memory>
#include <algorithm>
#include <map>
#include <vector>

namespace {
using namespace battery;
constexpr wchar_t controller_class[]=L"WirelessBattery.Controller.1";
constexpr wchar_t panel_class[]=L"WirelessBattery.Panel.1";
constexpr UINT tray_message=WM_APP+1, complete_message=WM_APP+2, show_message=WM_APP+3;
constexpr UINT refresh_message=WM_APP+4, quit_message=WM_APP+5;
constexpr int refresh_id=101, interval_id=102, threshold_id=103, startup_id=104, notify_id=105, folder_id=106;
constexpr int menu_show=201,menu_refresh=202,menu_exit=203;
constexpr unsigned intervals[]={30,60,120,300,600,1800};
constexpr unsigned thresholds[]={10,15,20,30};
constexpr COLORREF bg=RGB(245,247,250), ink=RGB(27,39,55), muted=RGB(83,99,117), border=RGB(222,229,236);
HINSTANCE instance;HWND controller=nullptr,panel=nullptr;
UINT taskbar_message;NOTIFYICONDATAW tray{};HICON tray_icon=nullptr;
bool tray_added=false,busy=false,quitting=false;
std::thread worker;
Settings settings;
Snapshot snapshot;
HFONT normal_font=nullptr,title_font=nullptr,number_font=nullptr,small_font=nullptr;
HBRUSH background_brush=nullptr,white_brush=nullptr;
UINT panel_dpi=96;
int scroll_position=0,content_height=0;
HWND refresh_button=nullptr,interval_combo=nullptr,threshold_combo=nullptr,startup_check=nullptr,notify_check=nullptr,footer=nullptr;
struct Row{HWND name,status,source,value;};std::vector<Row> rows;
std::map<std::wstring,ULONGLONG> notifications;
struct Completed{Snapshot snapshot;Bindings bindings;};
int px(int n){return MulDiv(n,static_cast<int>(panel_dpi),96);}
COLORREF reading_color(const Reading& r){if(!r.percent)return muted;if(r.charging.value_or(false))return RGB(29,94,178);if(*r.percent<=20)return RGB(182,48,48);if(*r.percent<40)return RGB(145,92,11);return RGB(27,113,84);}
void text(HWND hwnd,const std::wstring& value){if(hwnd)SetWindowTextW(hwnd,value.c_str());}
std::wstring clock_text(std::time_t at){if(!at)return L"";tm t{};localtime_s(&t,&at);wchar_t b[32]{};wcsftime(b,32,L"%H:%M:%S",&t);return b;}
void release_fonts(){for(auto f:{normal_font,title_font,number_font,small_font})if(f)DeleteObject(f);normal_font=title_font=number_font=small_font=nullptr;}
void create_fonts(){release_fonts();auto make=[](int size,int weight,const wchar_t* face){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);};normal_font=make(14,FW_NORMAL,L"Microsoft YaHei UI");title_font=make(25,FW_SEMIBOLD,L"Microsoft YaHei UI");number_font=make(34,FW_SEMIBOLD,L"Segoe UI");small_font=make(12,FW_NORMAL,L"Microsoft YaHei UI");}
HICON make_icon(){
    int size=GetSystemMetrics(SM_CXSMICON);if(size<16)size=16;
    return static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));
}
void update_tray(){
    auto icon=make_icon();
    tray={};tray.cbSize=sizeof(tray);tray.hWnd=controller;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=tray_message;tray.hIcon=icon;
    std::wstring tip=L"设备电源管理";
    for(const auto& r:snapshot.readings)tip+=L"\n"+r.name+L" "+(r.percent?std::to_wstring(*r.percent)+L"%":state_text(r));
    wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);
    if(!Shell_NotifyIconW(tray_added?NIM_MODIFY:NIM_ADD,&tray))tray_added=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;else tray_added=true;
    if(tray_icon)DestroyIcon(tray_icon);tray_icon=icon;
}
void notify_low(){
    if(!settings.notify || !tray_added)return;
    auto now=GetTickCount64();std::wstring message;
    for(const auto& r:snapshot.readings){
        if(r.percent && *r.percent>static_cast<int>(settings.threshold)+5){notifications.erase(r.id);continue;}
        if(!r.percent || r.charging.value_or(false) || r.full || *r.percent>static_cast<int>(settings.threshold))continue;
        auto it=notifications.find(r.id);if(it!=notifications.end() && now-it->second<2*60*60*1000)continue;
        if(message.size()<180){message+=r.name+L"："+std::to_wstring(*r.percent)+L"%\n";notifications[r.id]=now;}
    }
    if(message.empty())return;
    NOTIFYICONDATAW info{};info.cbSize=sizeof(info);info.hWnd=controller;info.uID=1;info.uFlags=NIF_INFO;info.dwInfoFlags=NIIF_WARNING|NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(info.szInfoTitle,L"设备电量偏低");wcsncpy_s(info.szInfo,message.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&info);
}
HWND child(const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0,HFONT font=nullptr){
    auto hwnd=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),panel,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font?font:normal_font),TRUE);return hwnd;
}
int setting_y(){return 94+static_cast<int>(rows.size())*112+16;}
void fill_rows(){
    for(size_t i=0;i<rows.size() && i<snapshot.readings.size();++i){auto& r=snapshot.readings[i];auto& row=rows[i];
        text(row.name,r.name);text(row.value,r.percent?std::to_wstring(*r.percent)+L"%":L"--");
        auto state=state_text(r);
        if(r.sampled)state+=L"   ·   "+clock_text(r.sampled);
        auto connection=r.transport==L"USB"?L"USB 连接":r.transport==L"HID"?L"HID 连接":L"2.4G 无线";
        text(row.status,state);text(row.source,r.detail.empty()?std::wstring(connection)+L"  ·  电量由设备提供":r.detail);
    }
    EnableWindow(refresh_button,!busy);text(refresh_button,busy?L"读取中…":L"立即刷新");
    text(footer,busy?L"正在读取设备，请稍候…":L"关闭窗口后在托盘继续运行。右键托盘可退出。");
    InvalidateRect(panel,nullptr,FALSE);
}
void destroy_children(){HWND w=GetWindow(panel,GW_CHILD);while(w){auto next=GetWindow(w,GW_HWNDNEXT);DestroyWindow(w);w=next;}rows.clear();}
void scroll_to(HWND hwnd,int next){
    SCROLLINFO si{sizeof(si),SIF_PAGE};GetScrollInfo(hwnd,SB_VERT,&si);
    next=std::clamp(next,0,std::max(0,content_height-static_cast<int>(si.nPage)));
    int delta=scroll_position-next;if(!delta)return;scroll_position=next;
    for(HWND child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT r;GetWindowRect(child,&r);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);SetWindowPos(child,nullptr,r.left,r.top+delta,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
    si.fMask=SIF_POS;si.nPos=next;SetScrollInfo(hwnd,SB_VERT,&si,TRUE);InvalidateRect(hwnd,nullptr,TRUE);
}
void reveal_focus(){
    if(!panel)return;HWND focused=GetFocus();if(!focused||!IsChild(panel,focused))return;
    RECT r,client;GetWindowRect(focused,&r);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT*>(&r),2);GetClientRect(panel,&client);
    if(r.top<0||r.bottom>client.bottom)scroll_to(panel,scroll_position+(r.top<0?r.top:r.bottom-client.bottom));
}
void layout_panel(){
    destroy_children();create_fonts();scroll_position=0;
    child(L"STATIC",L"设备电源管理",SS_LEFT,24,18,250,38,0,title_font);
    child(L"STATIC",L"你的设备，随时有数。",SS_LEFT,25,61,400,22,0,small_font);
    size_t count=std::max<size_t>(4,snapshot.readings.size());
    for(size_t i=0;i<count;++i){int y=94+static_cast<int>(i)*112;
        rows.push_back({child(L"STATIC",L"设备",SS_LEFT,40,y+14,370,23),child(L"STATIC",L"等待首次读取",SS_LEFT,40,y+42,370,21,0,small_font),
            child(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS,40,y+67,480,19,0,small_font),child(L"STATIC",L"--",SS_RIGHT,426,y+13,130,44,0,number_font)});
    }
    int y=setting_y();
    child(L"STATIC",L"刷新间隔",0,26,y+5,75,22,0,small_font);
    interval_combo=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,104,y,116,200,interval_id);
    const wchar_t* names[]={L"30 秒",L"1 分钟",L"2 分钟",L"5 分钟",L"10 分钟",L"30 分钟"};
    for(int i=0;i<6;++i){SendMessageW(interval_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(names[i]));if(intervals[i]==settings.interval)SendMessageW(interval_combo,CB_SETCURSEL,i,0);}
    child(L"STATIC",L"低电量",0,246,y+5,68,22,0,small_font);
    threshold_combo=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,311,y,85,180,threshold_id);
    for(int i=0;i<4;++i){auto label=std::to_wstring(thresholds[i])+L"%";SendMessageW(threshold_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(thresholds[i]==settings.threshold)SendMessageW(threshold_combo,CB_SETCURSEL,i,0);}
    refresh_button=child(L"BUTTON",L"立即刷新",BS_PUSHBUTTON|WS_TABSTOP,434,y-1,140,32,refresh_id);
    startup_check=child(L"BUTTON",L"开机启动",BS_AUTOCHECKBOX|WS_TABSTOP,26,y+43,130,28,startup_id);
    SendMessageW(startup_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);
    notify_check=child(L"BUTTON",L"低电量提醒",BS_AUTOCHECKBOX|WS_TABSTOP,174,y+43,145,28,notify_id);
    SendMessageW(notify_check,BM_SETCHECK,settings.notify?BST_CHECKED:BST_UNCHECKED,0);
    child(L"BUTTON",L"设置目录",BS_PUSHBUTTON|WS_TABSTOP,434,y+43,140,28,folder_id);
    footer=child(L"STATIC",L"",0,26,y+88,550,24,0,small_font);
    content_height=px(y+125);
    RECT size{0,0,px(600)+GetSystemMetricsForDpi(SM_CXVSCROLL,panel_dpi),content_height};
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL;
    AdjustWindowRectExForDpi(&size,style,FALSE,0,panel_dpi);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST),&monitor);
    RECT current{};GetWindowRect(panel,&current);
    int height=std::min<int>(size.bottom-size.top,monitor.rcWork.bottom-monitor.rcWork.top-px(16));
    int width=size.right-size.left;
    int left=std::clamp<int>(current.left,monitor.rcWork.left,std::max<int>(monitor.rcWork.left,monitor.rcWork.right-width));
    int top=std::clamp<int>(current.top,monitor.rcWork.top,std::max<int>(monitor.rcWork.top,monitor.rcWork.bottom-height));
    SetWindowPos(panel,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(panel,&client);SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,content_height-1,static_cast<UINT>(client.bottom),0,0};SetScrollInfo(panel,SB_VERT,&si,TRUE);
    fill_rows();
}
void refresh(){
    if(busy||quitting)return;if(worker.joinable())worker.join();busy=true;
    if(panel)fill_rows();
    auto bindings=settings.bindings;
    worker=std::thread([bindings]{
        auto done=std::make_unique<Completed>();
        try{Scanner scanner(bindings);done->snapshot=scanner.scan();done->bindings=scanner.bindings();}
        catch(...){done->snapshot.readings.clear();done->snapshot.trace.push_back(L"读取异常，请重新刷新");}
        if(PostMessageW(controller,complete_message,0,reinterpret_cast<LPARAM>(done.get())))done.release();
    });
}
void show_panel();
void persist(){if(!save_settings(settings)&&panel)MessageBoxW(panel,L"设置保存失败，请检查本地应用数据目录的写入权限。",L"设备电源管理",MB_OK|MB_ICONWARNING);}
LRESULT CALLBACK PanelProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:panel=hwnd;panel_dpi=GetDpiForWindow(hwnd);layout_panel();return 0;
    case WM_DPICHANGED:{panel_dpi=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout_panel();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_VSCROLL:{SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&si);int next=scroll_position;
        switch(LOWORD(wp)){case SB_LINEUP:next-=px(36);break;case SB_LINEDOWN:next+=px(36);break;case SB_PAGEUP:next-=si.nPage;break;case SB_PAGEDOWN:next+=si.nPage;break;case SB_THUMBTRACK:next=si.nTrackPos;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=content_height;break;}
        scroll_to(hwnd,next);return 0;}
    case WM_MOUSEWHEEL:{int steps=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;for(int i=0;i<std::abs(steps)*3;++i)SendMessageW(hwnd,WM_VSCROLL,steps>0?SB_LINEUP:SB_LINEDOWN,0);return 0;}
    case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);auto control=reinterpret_cast<HWND>(lp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,muted);
        for(size_t i=0;i<rows.size();++i){auto& row=rows[i];if(control==row.name){SetTextColor(dc,ink);return reinterpret_cast<LRESULT>(white_brush);}if(control==row.value){SetTextColor(dc,i<snapshot.readings.size()?reading_color(snapshot.readings[i]):muted);return reinterpret_cast<LRESULT>(white_brush);}if(control==row.status||control==row.source)return reinterpret_cast<LRESULT>(white_brush);}
        return reinterpret_cast<LRESULT>(background_brush);}
    case WM_CTLCOLORBTN:SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);return reinterpret_cast<LRESULT>(background_brush);
    case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);RECT client;GetClientRect(hwnd,&client);FillRect(dc,&client,background_brush);
        auto pen=CreatePen(PS_SOLID,1,border);auto oldpen=SelectObject(dc,pen);auto oldbrush=SelectObject(dc,white_brush);
        for(size_t i=0;i<rows.size();++i){int y=94+static_cast<int>(i)*112;RoundRect(dc,px(24),px(y)-scroll_position,px(576),px(y+101)-scroll_position,px(12),px(12));
            RECT track{px(40),px(y+90)-scroll_position,px(560),px(y+93)-scroll_position};auto brush=CreateSolidBrush(RGB(231,237,243));FillRect(dc,&track,brush);DeleteObject(brush);
            if(i<snapshot.readings.size()&&snapshot.readings[i].percent){track.right=track.left+MulDiv(px(520),*snapshot.readings[i].percent,100);brush=CreateSolidBrush(reading_color(snapshot.readings[i]));FillRect(dc,&track,brush);DeleteObject(brush);}
        }SelectObject(dc,oldpen);SelectObject(dc,oldbrush);DeleteObject(pen);EndPaint(hwnd,&ps);return 0;}
    case WM_COMMAND:{int id=LOWORD(wp),event=HIWORD(wp);
        if(id==refresh_id && event==BN_CLICKED)refresh();
        else if(id==interval_id&&event==CBN_SELCHANGE){auto i=SendMessageW(interval_combo,CB_GETCURSEL,0,0);if(i>=0&&i<6){settings.interval=intervals[i];SetTimer(controller,1,settings.interval*1000,nullptr);persist();}}
        else if(id==threshold_id&&event==CBN_SELCHANGE){auto i=SendMessageW(threshold_combo,CB_GETCURSEL,0,0);if(i>=0&&i<4){settings.threshold=thresholds[i];persist();}}
        else if(id==notify_id&&event==BN_CLICKED){settings.notify=SendMessageW(notify_check,BM_GETCHECK,0,0)==BST_CHECKED;persist();}
        else if(id==startup_id&&event==BN_CLICKED){bool enabled=SendMessageW(startup_check,BM_GETCHECK,0,0)==BST_CHECKED;auto hr=set_startup(enabled);if(FAILED(hr))MessageBoxW(hwnd,(L"无法更新开机启动："+error_text(HRESULT_CODE(hr))).c_str(),L"设备电源管理",MB_ICONWARNING);SendMessageW(startup_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);}
        else if(id==folder_id&&event==BN_CLICKED){auto path=data_folder();ShellExecuteW(hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        return 0;}
    case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE&&startup_check)SendMessageW(startup_check,BM_SETCHECK,startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);break;
    case WM_NEXTDLGCTL:{LRESULT result=DefWindowProcW(hwnd,msg,wp,lp);HWND focused=GetFocus();if(focused&&IsChild(hwnd,focused)){RECT r,client;GetWindowRect(focused,&r);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);GetClientRect(hwnd,&client);if(r.top<0||r.bottom>client.bottom)scroll_to(hwnd,scroll_position+(r.top<0?r.top:r.bottom-client.bottom));}return result;}
    case WM_SYSCOMMAND:if((wp&0xfff0)==SC_MINIMIZE){DestroyWindow(hwnd);return 0;}break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:rows.clear();panel=nullptr;refresh_button=footer=startup_check=notify_check=interval_combo=threshold_combo=nullptr;release_fonts();return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
void show_panel(){
    if(!panel){panel_dpi=96;HWND hwnd=CreateWindowExW(0,panel_class,L"设备电源管理",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,600,750,nullptr,nullptr,instance,nullptr);if(hwnd)ShowWindow(hwnd,SW_SHOWNORMAL);}
    if(panel){ShowWindow(panel,SW_RESTORE);SetForegroundWindow(panel);}
}
void menu(){
    HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,menu_show,L"打开电量面板");AppendMenuW(m,MF_STRING|(busy?MF_GRAYED:0),menu_refresh,L"立即刷新");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,menu_exit,L"退出");
    POINT p;GetCursorPos(&p);SetForegroundWindow(controller);int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,controller,nullptr);DestroyMenu(m);PostMessageW(controller,WM_NULL,0,0);
    if(id==menu_show)show_panel();if(id==menu_refresh)refresh();if(id==menu_exit)PostMessageW(controller,WM_CLOSE,0,0);
}
LRESULT CALLBACK ControllerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==taskbar_message){tray_added=false;update_tray();return 0;}
    switch(msg){
    case show_message:show_panel();return 0;
    case refresh_message:refresh();return 0;
    case quit_message:PostMessageW(hwnd,WM_CLOSE,0,0);return 0;
    case tray_message:if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)show_panel();else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)menu();return 0;
    case complete_message:{std::unique_ptr<Completed> done(reinterpret_cast<Completed*>(lp));if(worker.joinable())worker.join();busy=false;
        if(!done->snapshot.readings.empty()){snapshot=std::move(done->snapshot);if(settings.bindings!=done->bindings){settings.bindings=std::move(done->bindings);persist();}}
        else {for(auto& r:snapshot.readings){r.percent.reset();r.charging.reset();r.full=false;r.sampled=0;r.state=State::Error;r.detail=L"读取发生异常，请重试";}}
        if(panel){if(rows.size()!=snapshot.readings.size())layout_panel();else fill_rows();}update_tray();notify_low();return 0;}
    case WM_TIMER:if(wp==1||wp==2){if(wp==2)KillTimer(hwnd,2);refresh();}return 0;
    case WM_DEVICECHANGE:if(wp==DBT_DEVNODES_CHANGED||wp==DBT_DEVICEARRIVAL||wp==DBT_DEVICEREMOVECOMPLETE)SetTimer(hwnd,2,1200,nullptr);return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC)SetTimer(hwnd,2,1500,nullptr);return TRUE;
    case WM_CLOSE:quitting=true;KillTimer(hwnd,1);KillTimer(hwnd,2);if(panel)DestroyWindow(panel);if(worker.joinable())worker.join();
        {MSG pending;while(PeekMessageW(&pending,hwnd,complete_message,complete_message,PM_REMOVE))delete reinterpret_cast<Completed*>(pending.lParam);}
        if(tray_added)Shell_NotifyIconW(NIM_DELETE,&tray);tray_added=false;DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR command,int){
    instance=app;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto existing=FindWindowW(controller_class,nullptr);std::wstring args=command;
    if(args==L"--exit"||args==L"--refresh"){if(existing)PostMessageW(existing,args==L"--exit"?quit_message:refresh_message,0,0);return 0;}
    Handle mutex(CreateMutexW(nullptr,TRUE,L"Local\\WirelessBattery.Native.1"));
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){if(existing)PostMessageW(existing,show_message,0,0);return 0;}
    auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    settings=load_settings();taskbar_message=RegisterWindowMessageW(L"TaskbarCreated");
    background_brush=CreateSolidBrush(bg);white_brush=CreateSolidBrush(RGB(255,255,255));
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=ControllerProc;wc.lpszClassName=controller_class;RegisterClassExW(&wc);
    wc.lpfnWndProc=PanelProc;wc.lpszClassName=panel_class;wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;RegisterClassExW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW,controller_class,L"WirelessBattery Controller",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!controller){if(SUCCEEDED(com))CoUninitialize();return 1;}
    // Populate the initial state before the first asynchronous read finishes.
    for(auto pair:{std::pair{L"hyperx",L"飓风 3 Wireless"},std::pair{L"sora",L"Sora V2"},std::pair{L"z75s",L"迈从 Z75S"},std::pair{L"b98",L"iLovbee B98"}}){Reading r;r.id=pair.first;r.name=pair.second;r.detail=L"等待首次读取";snapshot.readings.push_back(r);}
    update_tray();SetTimer(controller,1,settings.interval*1000,nullptr);
    if(args!=L"--tray")show_panel();refresh();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(panel&&IsDialogMessageW(panel,&msg))reveal_focus();else{TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(tray_icon)DestroyIcon(tray_icon);DeleteObject(background_brush);DeleteObject(white_brush);if(SUCCEEDED(com))CoUninitialize();
    return 0;
}
