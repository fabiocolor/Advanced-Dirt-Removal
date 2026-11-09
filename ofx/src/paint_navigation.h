#pragma once
// Optional Resolve Color/Edit navigation, outside OFX callbacks. The bundled
// host interpreter runs the documented scripting API without a Python install.
#include <condition_variable>
#include <optional>
#include <filesystem>
#include <iomanip>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
extern char** environ;
#endif
namespace paint_navigation {
struct Request {std::string owner;int current,target,first,last;};
inline std::filesystem::path interpreter(){
 auto host=std::filesystem::path(adrlog::executablePath()).parent_path();
#ifdef __APPLE__
 return host/"../Libraries/Fusion/fuscript";
#elif defined(_WIN32)
 return host/"fuscript.exe";
#else
 return host/"libs/Fusion/fuscript";
#endif
}
inline std::filesystem::path script(){return std::filesystem::path(adrlog::pluginPath()).parent_path().parent_path()/"Resources/paint-navigation.lua";}
inline std::wstring quote(const std::wstring&s){
 std::wstring out=L"\"";size_t slashes=0;
 for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')out+=L'\\';out+=c;}
 out.append(slashes*2,L'\\');return out+L'"';
}
class Worker {
 std::mutex lock;std::condition_variable ready;std::optional<Request> pending;std::thread thread;std::atomic<bool> stopping{false};
 void run(){for(;;){Request r;{std::unique_lock<std::mutex>g(lock);ready.wait(g,[&]{return stopping||pending.has_value();});if(stopping)return;r=std::move(*pending);pending.reset();}execute(r);}}
 void execute(const Request&r){
  std::vector<std::string>a={interpreter().string(),"-l","lua",script().string(),"--seek",r.owner,std::to_string(r.current),std::to_string(r.target),std::to_string(r.first),std::to_string(r.last)};
  int result=-1;
#ifdef _WIN32
  std::wstring command=quote(interpreter().wstring());for(size_t n=1;n<a.size();++n){auto w=std::filesystem::u8path(a[n]).wstring();command+=L" "+quote(w);}
  STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
  if(CreateProcessW(interpreter().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
   auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   while(WaitForSingleObject(process.hProcess,25)==WAIT_TIMEOUT){if(stopping||std::chrono::steady_clock::now()>end){TerminateProcess(process.hProcess,1);break;}}
   DWORD code=1;GetExitCodeProcess(process.hProcess,&code);result=int(code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
  }
#else
  std::vector<char*>args;for(auto&v:a)args.push_back(v.data());args.push_back(nullptr);
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0);posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
  pid_t child=0;int error=posix_spawn(&child,args[0],&actions,nullptr,args.data(),environ);posix_spawn_file_actions_destroy(&actions);
  if(!error){int status=0;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   for(;;){auto waited=waitpid(child,&status,WNOHANG);if(waited==child){result=WIFEXITED(status)?WEXITSTATUS(status):-1;break;}if(waited<0)break;
    if(stopping||std::chrono::steady_clock::now()>end){kill(child,SIGKILL);waitpid(child,&status,0);break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  }
#endif
  if(!stopping)adrlog::info("paint navigation: "+std::string(result==0?"moved to stroke frame ":"could not move to stroke frame ")+std::to_string(r.target)+(result==0?"":" (Resolve scripting unavailable or clip changed)"));
 }
public:
 ~Worker(){stop();}
 bool request(Request r){
  if(!std::filesystem::is_regular_file(interpreter())||!std::filesystem::is_regular_file(script()))return false;
  std::lock_guard<std::mutex>g(lock);if(stopping)return false;pending=std::move(r);if(!thread.joinable())thread=std::thread([this]{run();});ready.notify_one();return true;
 }
 void stop(){stopping=true;ready.notify_one();if(thread.joinable())thread.join();}
};
inline std::unique_ptr<Worker>& worker(){static std::unique_ptr<Worker>value;return value;}
inline bool request(void*owner,double current,double target,int first,int last){
 if(!std::isfinite(current)||!std::isfinite(target)||current!=std::round(current)||target!=std::round(target))return false;
 static const auto session=std::chrono::steady_clock::now().time_since_epoch().count();
 std::ostringstream token;token<<adrlog::pid()<<'-'<<session<<'-'<<owner;
 if(!worker())worker()=std::make_unique<Worker>();return worker()->request({token.str(),int(current),int(target),first,last});
}
inline void shutdown(){if(worker()){worker()->stop();worker().reset();}}
}
