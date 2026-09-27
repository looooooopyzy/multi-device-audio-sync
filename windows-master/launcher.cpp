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
struct App {
  HWND window=nullptr,status=nullptr,setup=nullptr,secure=nullptr;
  HWND mode=nullptr,output=nullptr;
  HFONT font=nullptr;
  HANDLE master=nullptr,proxy=nullptr;
  std::filesystem::path root,master_exe,proxy_script,pythonw;
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
void set_status(const std::wstring& value) { SetWindowTextW(app.status,value.c_str()); }
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
UINT default_output_device() {
  IMMDeviceEnumerator* enumerator=nullptr;
  IMMDevice* endpoint=nullptr;
  IPropertyStore* properties=nullptr;
  PROPVARIANT value{};
  UINT result=WAVE_MAPPER;
  if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) &&
     SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&endpoint)) &&
     SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ,&properties)) &&
     SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value)) &&
     value.vt==VT_LPWSTR && value.pwszVal) {
    const std::wstring name=value.pwszVal;
    for(UINT id=0;id<waveOutGetNumDevs();++id) {
      WAVEOUTCAPSW caps{};
      if(waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
         name.rfind(caps.szPname,0)==0) { result=id; break; }
    }
  }
  PropVariantClear(&value);
  if(properties) properties->Release();
  if(endpoint) endpoint->Release();
  if(enumerator) enumerator->Release();
  return result;
}
bool default_is_virtual_route_input() {
  const UINT id=default_output_device();
  WAVEOUTCAPSW caps{};
  return id!=WAVE_MAPPER && waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
          is_virtual_route_input(caps.szPname);
}
void populate_output_devices() {
  const UINT count=waveOutGetNumDevs();
  std::vector<std::pair<UINT,std::wstring>> devices;
  for(UINT id=0;id<count;++id) {
    WAVEOUTCAPSW caps{};
    if(waveOutGetDevCapsW(id,&caps,sizeof(caps))==MMSYSERR_NOERROR &&
       !is_likely_virtual_output(caps.szPname))
      devices.emplace_back(id,caps.szPname);
  }
  const UINT default_id=default_output_device();
  int selected=-1,first_physical=-1;
  for(const auto& device:devices) {
    int row=static_cast<int>(SendMessageW(app.output,CB_ADDSTRING,0,
                                          reinterpret_cast<LPARAM>(device.second.c_str())));
    SendMessageW(app.output,CB_SETITEMDATA,row,device.first);
    if(first_physical<0) first_physical=row;
    if(device.first==default_id) selected=row;
  }
  if(selected<0) selected=first_physical;
  if(selected<0 && !devices.empty()) selected=0;
  if(selected>=0) {
    SendMessageW(app.output,CB_SETCURSEL,selected,0);
    app.output_device=static_cast<uint32_t>(SendMessageW(app.output,CB_GETITEMDATA,selected,0));
  }
}
void start_services() {
  stop_services();
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
  app.route_music=app.music_mode && default_is_virtual_route_input() &&
                  app.output_device!=default_output_device();
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
                 int x,int y,int width,int height,int id=0) {
  HWND control=CreateWindowExW(0,kind,value,WS_CHILD|WS_VISIBLE|style,
    x,y,width,height,app.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
    GetModuleHandleW(nullptr),nullptr);
  SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);
  return control;
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  switch(message) {
  case WM_CREATE: {
    app.window=window;
    app.font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    add_control(L"STATIC",L"双向声音自动校准",0,24,18,650,34);
    add_control(L"STATIC",L"Windows 主控与 HTTPS 会自动在后台运行，无需打开终端。",0,24,57,660,28);
    add_control(L"STATIC",L"iPad 首次设置地址（先安装证书）",0,24,101,650,28);
    app.setup=add_control(L"EDIT",L"正在准备…",ES_READONLY|WS_BORDER|ES_AUTOHSCROLL,
                          24,130,515,34);
    add_control(L"BUTTON",L"复制",BS_PUSHBUTTON,551,130,122,34,kCopySetup);
    add_control(L"STATIC",L"已信任证书后使用的校准地址",0,24,179,650,28);
    app.secure=add_control(L"EDIT",L"正在准备…",ES_READONLY|WS_BORDER|ES_AUTOHSCROLL,
                           24,208,515,34);
    add_control(L"BUTTON",L"复制",BS_PUSHBUTTON,551,208,122,34,kCopySecure);
    add_control(L"BUTTON",L"打开证书目录",BS_PUSHBUTTON,24,268,165,38,kOpenFolder);
    add_control(L"BUTTON",L"重新启动服务",BS_PUSHBUTTON,202,268,165,38,kRestart);
    add_control(L"STATIC",L"音源",0,390,247,280,22);
    app.mode=add_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,
      390,270,283,140,kMode);
    SendMessageW(app.mode,CB_ADDSTRING,0,
      reinterpret_cast<LPARAM>(L"测试音（用于校准）"));
    SendMessageW(app.mode,CB_ADDSTRING,0,
      reinterpret_cast<LPARAM>(L"电脑正在播放的音乐"));
    SendMessageW(app.mode,CB_SETCURSEL,app.music_mode?1:0,0);
    add_control(L"STATIC",L"电脑扬声器输出",0,24,247,330,22);
    app.output=add_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,
      24,270,340,140,kOutput);
    populate_output_devices();
    app.status=add_control(L"STATIC",L"准备启动…",0,24,326,650,62);
    start_services();
    return 0;
  }
  case WM_TIMER:
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
             L"双端受控播放已就绪。Windows 默认输出为虚拟设备，选择的设备播放电脑原声；现在可点页面自动校准。":
             L"当前可播放到 iPad。若要与电脑扬声器同步，请将 VB-CABLE 或 Virtual Audio Driver 设为默认播放设备，并选择真实扬声器。"));
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
        const LRESULT row=SendMessageW(app.output,CB_GETCURSEL,0,0);
        if(row>=0) {
          const uint32_t selected=static_cast<uint32_t>(
            SendMessageW(app.output,CB_GETITEMDATA,row,0));
          if(selected!=app.output_device) { app.output_device=selected; start_services(); }
        }
      }
      return 0;
    }
    break;
  case WM_DESTROY:
    KillTimer(window,1);
    stop_services();
    if(app.font) DeleteObject(app.font);
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
  klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  klass.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
  RegisterClassW(&klass);
  HWND window=CreateWindowExW(0,klass.lpszClassName,L"多设备音频 · 自动校准",
    WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,
    710,440,nullptr,nullptr,instance,nullptr);
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
