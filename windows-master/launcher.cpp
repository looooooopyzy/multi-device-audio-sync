#include "network_addresses.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <filesystem>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {
constexpr int kCopySetup=101,kCopySecure=102,kOpenFolder=103,kRestart=104,kMode=105,kOutput=106;
constexpr COLORREF kBackground=RGB(244,248,250);
constexpr COLORREF kCard=RGB(255,255,255);
constexpr COLORREF kBorder=RGB(215,226,231);
constexpr COLORREF kText=RGB(25,45,53);
constexpr COLORREF kMuted=RGB(93,112,120);
constexpr COLORREF kAccent=RGB(0,153,139);
constexpr COLORREF kWarning=RGB(171,98,25);
struct App {
  HWND window=nullptr,title=nullptr,subtitle=nullptr,note=nullptr;
  HWND status=nullptr,setup=nullptr,secure=nullptr;
  HWND mode=nullptr,output=nullptr;
  HFONT font=nullptr,title_font=nullptr,section_font=nullptr,small_font=nullptr;
  HBRUSH background_brush=nullptr,card_brush=nullptr;
  COLORREF status_color=kText;
  HANDLE master=nullptr,proxy=nullptr;
  std::filesystem::path root,master_exe,proxy_script,pythonw;
  std::wstring default_endpoint_id;
  std::string ip;
  int http_port=0,https_port=0;
  bool music_mode=true;
  uint32_t output_device=0xffffffffu;
  bool route_music=false;
  std::wstring setup_url,secure_url;
} app;

std::wstring quote(const std::wstring& value) { return L"\""+value+L"\""; }
std::wstring widen(const std::string& value) {
  return std::wstring(value.begin(),value.end()); // LAN addresses are ASCII IPv4.
}
std::filesystem::path executable_path() {
  std::wstring path(32768,L'\0');
  DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  path.resize(length);
  return path;
}
std::filesystem::path find_root(std::filesystem::path from) {
  for(int i=0;i<6;++i) {
    if(std::filesystem::exists(from/L"web-client/index.html") &&
       std::filesystem::exists(from/L"tools/secure_web.py")) return from;
    if(!from.has_parent_path()) break;
    from=from.parent_path();
  }
  return {};
}
std::filesystem::path find_pythonw() {
  std::wstring path(32768,L'\0');
  DWORD length=SearchPathW(nullptr,L"pythonw.exe",nullptr,
                           static_cast<DWORD>(path.size()),path.data(),nullptr);
  if(length>0 && length<path.size()) { path.resize(length); return path; }
  return {};
}
bool free_port(int port) {
  SOCKET socket_value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(socket_value==INVALID_SOCKET) return false;
  BOOL exclusive=TRUE;
  setsockopt(socket_value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,
             reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
  sockaddr_in address{}; address.sin_family=AF_INET;
  address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(static_cast<u_short>(port));
  bool available=bind(socket_value,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0;
  closesocket(socket_value);
  return available;
}
bool listening(int port) {
  SOCKET socket_value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(socket_value==INVALID_SOCKET) return false;
  sockaddr_in address{}; address.sin_family=AF_INET;
  address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  address.sin_port=htons(static_cast<u_short>(port));
  bool ready=connect(socket_value,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0;
  closesocket(socket_value);
  return ready;
}
bool alive(HANDLE process) {
  DWORD code=0;
  return process && GetExitCodeProcess(process,&code) && code==STILL_ACTIVE;
}
void stop_process(HANDLE& process) {
  if(!process) return;
  if(alive(process)) {
    TerminateProcess(process,0);
    WaitForSingleObject(process,3000);
  }
  CloseHandle(process);
  process=nullptr;
}
bool launch(const std::filesystem::path& executable,const std::wstring& arguments,
            HANDLE& process) {
  std::wstring command=quote(executable.wstring())+L" "+arguments;
  STARTUPINFOW startup{}; startup.cb=sizeof(startup);
  PROCESS_INFORMATION info{};
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,
                     CREATE_NO_WINDOW,nullptr,app.root.c_str(),&startup,&info)) return false;
  process=info.hProcess;
  CloseHandle(info.hThread);
  return true;
}
void set_status(const std::wstring& value) {
  app.status_color=value.find(L"已就绪")!=std::wstring::npos ||
                   value.find(L"校准完成")!=std::wstring::npos?kAccent:
                   value.find(L"失败")!=std::wstring::npos ||
                   value.find(L"退出")!=std::wstring::npos ||
                   value.find(L"找不到")!=std::wstring::npos ||
                   value.find(L"仅 iPad")!=std::wstring::npos?kWarning:kText;
  SetWindowTextW(app.status,value.c_str());
}
std::wstring calibration_status() {
  std::ifstream file(app.root/L".local-certs/calibration-status.txt",std::ios::binary);
  if(!file) return L"等待 iPad 发起校准";
  std::string utf8(512,'\0');
  file.read(utf8.data(),static_cast<std::streamsize>(utf8.size()));
  utf8.resize(static_cast<size_t>(file.gcount()));
  if(utf8.empty()) return L"等待 iPad 发起校准";
  int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),
                                static_cast<int>(utf8.size()),nullptr,0);
  if(count<=0) return L"正在读取校准状态";
  std::wstring result(static_cast<size_t>(count),L'\0');
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),
                      static_cast<int>(utf8.size()),result.data(),count);
  return result;
}
void stop_services() {
  stop_process(app.proxy);
  stop_process(app.master);
}
bool is_virtual_route_input(const std::wstring& name) {
  std::wstring lower=name;
  for(auto& ch:lower) if(ch>=L'A' && ch<=L'Z') ch+=L'a'-L'A';
  return (lower.find(L"cable input")!=std::wstring::npos &&
          lower.find(L"vb-audio")!=std::wstring::npos) ||
         lower.find(L"virtual audio driver")!=std::wstring::npos;
}
bool is_likely_virtual_output(const std::wstring& name) {
  std::wstring lower=name;
  for(auto& ch:lower) if(ch>=L'A' && ch<=L'Z') ch+=L'a'-L'A';
  return lower.find(L"virtual")!=std::wstring::npos ||
         lower.find(L"cable")!=std::wstring::npos ||
         lower.find(L"transcreen")!=std::wstring::npos ||
         lower.find(L"虚拟")!=std::wstring::npos;
}
struct DefaultOutputEndpoint {
  std::wstring id,name;
};
DefaultOutputEndpoint default_output_endpoint() {
  DefaultOutputEndpoint result;
  IMMDeviceEnumerator* enumerator=nullptr;
  IMMDevice* endpoint=nullptr;
  IPropertyStore* properties=nullptr;
  PROPVARIANT value{};
  if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) &&
     SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&endpoint))) {
    LPWSTR id=nullptr;
    if(SUCCEEDED(endpoint->GetId(&id)) && id) {
      result.id=id;
      CoTaskMemFree(id);
    }
    if(SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ,&properties)) &&
       SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value)) &&
       value.vt==VT_LPWSTR && value.pwszVal) result.name=value.pwszVal;
  }
  PropVariantClear(&value);
  if(properties) properties->Release();
  if(endpoint) endpoint->Release();
  if(enumerator) enumerator->Release();
  return result;
}
UINT default_output_device() {
  const auto name=default_output_endpoint().name;
  for(UINT id=0;id<waveOutGetNumDevs();++id) {
    WAVEOUTCAPSW caps{};
    if(waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
       name.rfind(caps.szPname,0)==0) return id;
  }
  return WAVE_MAPPER;
}
void populate_output_devices() {
  std::wstring preferred;
  const LRESULT previous=SendMessageW(app.output,CB_GETCURSEL,0,0);
  if(previous>=0) {
    const LRESULT length=SendMessageW(app.output,CB_GETLBTEXTLEN,previous,0);
    if(length>0) {
      preferred.resize(static_cast<size_t>(length)+1);
      SendMessageW(app.output,CB_GETLBTEXT,previous,
                   reinterpret_cast<LPARAM>(preferred.data()));
      preferred.resize(static_cast<size_t>(length));
    }
  }
  SendMessageW(app.output,CB_RESETCONTENT,0,0);
  app.output_device=0xffffffffu;
  const UINT count=waveOutGetNumDevs();
  std::vector<std::pair<UINT,std::wstring>> devices;
  for(UINT id=0;id<count;++id) {
    WAVEOUTCAPSW caps{};
    if(waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
       !is_likely_virtual_output(caps.szPname))
      devices.emplace_back(id,caps.szPname);
  }
  const UINT default_id=default_output_device();
  int preferred_row=-1,default_row=-1,first_physical=-1;
  for(const auto& device:devices) {
    int row=static_cast<int>(SendMessageW(app.output,CB_ADDSTRING,0,
                                          reinterpret_cast<LPARAM>(device.second.c_str())));
    SendMessageW(app.output,CB_SETITEMDATA,row,device.first);
    if(first_physical<0) first_physical=row;
    if(device.second==preferred) preferred_row=row;
    if(device.first==default_id) default_row=row;
  }
  const int selected=preferred_row>=0?preferred_row:
                     default_row>=0?default_row:first_physical;
  if(selected>=0) {
    SendMessageW(app.output,CB_SETCURSEL,selected,0);
    app.output_device=static_cast<uint32_t>(SendMessageW(app.output,CB_GETITEMDATA,selected,0));
  }
}
void start_services() {
  KillTimer(app.window,1);
  stop_services();
  const auto endpoint=default_output_endpoint();
  app.default_endpoint_id=endpoint.id;
  populate_output_devices();
  auto addresses=audio::lan_addresses();
  app.ip.clear();
  for(const auto& address:addresses) if(address.preferred) { app.ip=address.ip; break; }
  if(app.ip.empty() && !addresses.empty()) app.ip=addresses.front().ip;
  if(app.ip.empty()) { set_status(L"未找到局域网 IPv4 地址。请先连接与 iPad 相同的 Wi-Fi。"); return; }
  if(app.root.empty() || !std::filesystem::exists(app.master_exe) ||
     !std::filesystem::exists(app.proxy_script)) {
    set_status(L"找不到主控或网页文件。请从完整项目目录运行本程序。"); return;
  }
  if(app.pythonw.empty()) {
    set_status(L"找不到 pythonw.exe。请安装 Python 和 tools/requirements.txt 后重启。"); return;
  }
  app.http_port=app.https_port=0;
  for(int i=0;i<30;++i) if(free_port(17891+i) && free_port(17901+i)) {
    app.http_port=17891+i; app.https_port=17901+i; break;
  }
  if(!app.http_port) { set_status(L"找不到空闲服务端口，请关闭旧主控后重试。"); return; }
  const std::wstring address=widen(app.ip);
  app.setup_url=L"http://"+address+L":"+std::to_wstring(app.http_port)+
                L"/setup.html?https="+std::to_wstring(app.https_port);
  app.secure_url=L"https://"+address+L":"+std::to_wstring(app.https_port)+L"/";
  SetWindowTextW(app.setup,app.setup_url.c_str());
  SetWindowTextW(app.secure,app.secure_url.c_str());
  if(app.output_device==0xffffffffu) {
    set_status(L"找不到可用的电脑扬声器输出设备。"); return;
  }
  const UINT default_device=default_output_device();
  app.route_music=app.music_mode && is_virtual_route_input(endpoint.name) &&
                  (default_device==WAVE_MAPPER || app.output_device!=default_device);
  const std::wstring master_args=(app.music_mode?
    L"--web-only --source system":L"--web-only --source tone")+
    (app.music_mode && !app.route_music?L"":
      L" --local-stream-output --render-device "+std::to_wstring(app.output_device))+
    L" --http-port "+std::to_wstring(app.http_port);
  if(!launch(app.master_exe,master_args,app.master)) {
    set_status(L"Windows 主控启动失败。请检查程序文件和防火墙设置。"); return;
  }
  const std::wstring proxy_args=quote(app.proxy_script.wstring())+L" --ip "+address+
    L" --port "+std::to_wstring(app.https_port)+L" --backend-port "+
    std::to_wstring(app.http_port);
  if(!launch(app.pythonw,proxy_args,app.proxy)) {
    stop_services(); set_status(L"HTTPS 服务启动失败。请检查 Python 环境。"); return;
  }
  set_status(app.music_mode?L"正在启动电脑音乐捕获与 HTTPS 服务…":
             L"正在启动主控、HTTPS 和麦克风校准服务…");
  SetTimer(app.window,1,1000,nullptr);
}
void copy_text(const std::wstring& value) {
  if(!OpenClipboard(app.window)) return;
  EmptyClipboard();
  const size_t size=(value.size()+1)*sizeof(wchar_t);
  HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);
  if(memory) {
    void* data=GlobalLock(memory);
    if(data) {
      memcpy(data,value.c_str(),size);
      GlobalUnlock(memory);
      if(!SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory);
    } else GlobalFree(memory);
  }
  CloseClipboard();
}
HWND add_control(const wchar_t* kind,const wchar_t* value,DWORD style,
                 int x,int y,int width,int height,int id=0,HFONT font=nullptr) {
  HWND control=CreateWindowExW(0,kind,value,WS_CHILD|WS_VISIBLE|style,
    x,y,width,height,app.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
    GetModuleHandleW(nullptr),nullptr);
  SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font?font:app.font),TRUE);
  return control;
}
void draw_card(HDC dc,int left,int top,int right,int bottom) {
  HPEN pen=CreatePen(PS_SOLID,1,kBorder);
  HGDIOBJ old_pen=SelectObject(dc,pen);
  HGDIOBJ old_brush=SelectObject(dc,app.card_brush);
  RoundRect(dc,left,top,right,bottom,16,16);
  SelectObject(dc,old_brush);
  SelectObject(dc,old_pen);
  DeleteObject(pen);
}
void draw_button(const DRAWITEMSTRUCT& item) {
  const bool primary=item.CtlID==kRestart;
  const bool pressed=(item.itemState&ODS_SELECTED)!=0;
  const COLORREF fill=primary?(pressed?RGB(0,127,116):kAccent):
                               (pressed?RGB(233,241,244):kCard);
  HBRUSH brush=CreateSolidBrush(fill);
  HPEN pen=CreatePen(PS_SOLID,1,primary?fill:kBorder);
  HGDIOBJ old_brush=SelectObject(item.hDC,brush);
  HGDIOBJ old_pen=SelectObject(item.hDC,pen);
  RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,
            item.rcItem.right-1,item.rcItem.bottom-1,10,10);
  SelectObject(item.hDC,old_brush);
  SelectObject(item.hDC,old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
  wchar_t label[100]{};
  GetWindowTextW(item.hwndItem,label,100);
  SetBkMode(item.hDC,TRANSPARENT);
  SetTextColor(item.hDC,primary?RGB(255,255,255):kText);
  HGDIOBJ old_font=SelectObject(item.hDC,app.font);
  RECT text=item.rcItem;
  DrawTextW(item.hDC,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  SelectObject(item.hDC,old_font);
  if(item.itemState&ODS_FOCUS) {
    RECT focus=item.rcItem;
    InflateRect(&focus,-5,-5);
    DrawFocusRect(item.hDC,&focus);
  }
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  switch(message) {
  case WM_CREATE: {
    app.window=window;
    app.background_brush=CreateSolidBrush(kBackground);
    app.card_brush=CreateSolidBrush(kCard);
    app.font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.title_font=CreateFontW(-31,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.section_font=CreateFontW(-21,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.small_font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.title=add_control(L"STATIC",L"多设备音频",0,32,20,770,39,0,app.title_font);
    app.subtitle=add_control(L"STATIC",L"Windows 与 iPad 同步播放 · 本地网络自动校准",
                             0,34,65,760,26,0,app.small_font);
    add_control(L"STATIC",L"连接 iPad",0,36,116,740,29,0,app.section_font);
    add_control(L"STATIC",L"首次使用：先打开这个地址安装并信任证书",0,
                36,149,740,23,0,app.small_font);
    app.setup=add_control(L"EDIT",L"正在准备…",ES_READONLY|WS_BORDER|ES_AUTOHSCROLL,
                          36,176,638,38);
    add_control(L"BUTTON",L"复制",BS_OWNERDRAW|WS_TABSTOP,690,176,96,38,kCopySetup);
    add_control(L"STATIC",L"后续使用：打开 HTTPS 校准页面",0,
                36,224,740,23,0,app.small_font);
    app.secure=add_control(L"EDIT",L"正在准备…",ES_READONLY|WS_BORDER|ES_AUTOHSCROLL,
                           36,251,638,38);
    add_control(L"BUTTON",L"复制",BS_OWNERDRAW|WS_TABSTOP,690,251,96,38,kCopySecure);
    add_control(L"STATIC",L"播放设置",0,36,335,740,29,0,app.section_font);
    add_control(L"STATIC",L"电脑扬声器输出",0,36,367,350,23,0,app.small_font);
    app.output=add_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,
      36,390,350,154,kOutput);
    add_control(L"STATIC",L"音源",0,430,367,350,23,0,app.small_font);
    app.mode=add_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,
      430,390,356,154,kMode);
    SendMessageW(app.mode,CB_ADDSTRING,0,
      reinterpret_cast<LPARAM>(L"测试音（用于校准）"));
    SendMessageW(app.mode,CB_ADDSTRING,0,
      reinterpret_cast<LPARAM>(L"电脑正在播放的音乐"));
    SendMessageW(app.mode,CB_SETCURSEL,app.music_mode?1:0,0);
    add_control(L"BUTTON",L"打开证书目录",BS_OWNERDRAW|WS_TABSTOP,
                36,471,208,42,kOpenFolder);
    add_control(L"BUTTON",L"重新启动服务",BS_OWNERDRAW|WS_TABSTOP,
                256,471,208,42,kRestart);
    app.note=add_control(L"STATIC",L"切换默认输出后会自动重连",0,
                         489,480,295,24,0,app.small_font);
    add_control(L"STATIC",L"运行状态",0,36,539,740,28,0,app.section_font);
    app.status=add_control(L"STATIC",L"准备启动…",SS_LEFT,36,570,750,39);
    start_services();
    return 0;
  }
  case WM_ERASEBKGND: {
    RECT area{};
    GetClientRect(window,&area);
    FillRect(reinterpret_cast<HDC>(wparam),&area,app.background_brush);
    return 1;
  }
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc=BeginPaint(window,&paint);
    draw_card(dc,20,103,820,307);
    draw_card(dc,20,321,820,451);
    draw_card(dc,20,526,820,614);
    EndPaint(window,&paint);
    return 0;
  }
  case WM_CTLCOLORSTATIC: {
    HDC dc=reinterpret_cast<HDC>(wparam);
    HWND control=reinterpret_cast<HWND>(lparam);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,control==app.title?kText:
                    control==app.status?app.status_color:kMuted);
    return reinterpret_cast<LRESULT>(control==app.title || control==app.subtitle ||
                                     control==app.note?
                                     app.background_brush:app.card_brush);
  }
  case WM_CTLCOLOREDIT: {
    HDC dc=reinterpret_cast<HDC>(wparam);
    SetBkColor(dc,kCard);
    SetTextColor(dc,kText);
    return reinterpret_cast<LRESULT>(app.card_brush);
  }
  case WM_DRAWITEM:
    draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
    return TRUE;
  case WM_TIMER:
    if(const auto endpoint=default_output_endpoint();
       !endpoint.id.empty() && endpoint.id!=app.default_endpoint_id) {
      start_services();
      return 0;
    }
    if(!alive(app.master)) {
      const auto source=calibration_status();
      set_status(source.find(L"Windows 输出设备启动失败")!=std::wstring::npos?source:
        L"Windows 主控已退出。点击“重新启动服务”重试。");
    }
    else if(!alive(app.proxy)) set_status(L"HTTPS 服务已退出。请检查 Python 的 cryptography 依赖。");
    else if(listening(app.http_port) && listening(app.https_port) &&
            std::filesystem::exists(app.root/L".local-certs/lan-root-ca.crt"))
      if(app.music_mode) {
        const auto source=calibration_status();
        set_status(source.find(L"电脑音乐捕获失败")!=std::wstring::npos?source:
          (app.route_music?
             L"双端受控播放已就绪。电脑扬声器和 iPad 已由主控同步播放，可在 iPad 页面自动校准。":
             L"仅 iPad 转发。若要电脑扬声器同步播放，请将 CABLE Input 设为 Windows 默认输出。"));
      } else set_status(L"服务已就绪。"+calibration_status()+L"。iPad 在校准页点击“自动校准”。");
    return 0;
  case WM_COMMAND:
    switch(LOWORD(wparam)) {
    case kCopySetup: copy_text(app.setup_url); return 0;
    case kCopySecure: copy_text(app.secure_url); return 0;
    case kOpenFolder: {
      auto folder=app.root/L".local-certs";
      ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
      return 0;
    }
    case kRestart: start_services(); return 0;
    case kMode:
      if(HIWORD(wparam)==CBN_SELCHANGE) {
        const bool selected=SendMessageW(app.mode,CB_GETCURSEL,0,0)==1;
        if(selected!=app.music_mode) { app.music_mode=selected; start_services(); }
      }
      return 0;
    case kOutput:
      if(HIWORD(wparam)==CBN_SELCHANGE) {
        start_services();
      }
      return 0;
    }
    break;
  case WM_DESTROY:
    KillTimer(window,1);
    stop_services();
    if(app.font) DeleteObject(app.font);
    if(app.title_font) DeleteObject(app.title_font);
    if(app.section_font) DeleteObject(app.section_font);
    if(app.small_font) DeleteObject(app.small_font);
    if(app.background_brush) DeleteObject(app.background_brush);
    if(app.card_brush) DeleteObject(app.card_brush);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window,message,wparam,lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR arguments,int show) {
  const HRESULT com_status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(arguments && std::wstring(arguments).find(L"--tone")!=std::wstring::npos)
    app.music_mode=false;
  WSADATA winsock{};
  if(WSAStartup(MAKEWORD(2,2),&winsock)!=0) {
    if(SUCCEEDED(com_status)) CoUninitialize();
    return 1;
  }
  const auto own_exe=executable_path();
  app.root=find_root(own_exe.parent_path());
  app.master_exe=own_exe.parent_path()/L"windows-master.exe";
  app.proxy_script=app.root/L"tools/secure_web.py";
  app.pythonw=find_pythonw();
  WNDCLASSW klass{};
  klass.lpfnWndProc=window_proc;
  klass.hInstance=instance;
  klass.lpszClassName=L"MultiDeviceAudioCalibrator";
  klass.hbrBackground=nullptr;
  klass.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
  RegisterClassW(&klass);
  HWND window=CreateWindowExW(0,klass.lpszClassName,L"多设备音频 · 自动校准",
    WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
    CW_USEDEFAULT,CW_USEDEFAULT,860,680,nullptr,nullptr,instance,nullptr);
  if(window) {
    ShowWindow(window,show);
    UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  WSACleanup();
  if(SUCCEEDED(com_status)) CoUninitialize();
  return 0;
}
