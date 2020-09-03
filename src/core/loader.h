#ifndef SRC_CORE_LOADER_H_
#define SRC_CORE_LOADER_H_

#include <atomic>
#include <mutex>
#include <dlfcn.h>

#define ONLD_TRACE(str) \
  if (getenv("ROCP_ONLOAD_TRACE")) do { \
    std::cout << "PID(" << GetPid() << "): TRACER_LOADER::" << __FUNCTION__ << " " << str << std::endl << std::flush; \
  } while(0);

namespace roctracer {

// Base runtime loader class
template <class T>
class BaseLoader : public T {
  static uint32_t GetPid() { return syscall(__NR_getpid); }

  public:
  typedef std::mutex mutex_t;
  typedef BaseLoader<T> loader_t;

  bool Enabled() const { return (handle_ != NULL); }

  template <class fun_t>
  fun_t* GetFun(const char* fun_name) {
    if (handle_ == NULL) return NULL;

    fun_t *f = (fun_t*) dlsym(handle_, fun_name);
    if ((to_check_symb_ == true) && (f == NULL)) {
      fprintf(stderr, "roctracer: symbol lookup '%s' failed: \"%s\"\n", fun_name, dlerror());
      abort();
    }
    return f;
  }

  static inline loader_t& Instance() {
    loader_t* obj = instance_.load(std::memory_order_acquire);
    if (obj == NULL) {
      std::lock_guard<mutex_t> lck(mutex_);
      if (instance_.load(std::memory_order_relaxed) == NULL) {
        obj = new loader_t();
        instance_.store(obj, std::memory_order_release);
      }
    }
    return *instance_;
  }

  static loader_t* GetRef() { return instance_; }
  static void SetLibName(const char *name) { lib_name_ = name; }

  private:
  BaseLoader() {
    const int flags = (to_load_ == true) ? RTLD_LAZY : RTLD_LAZY|RTLD_NOLOAD;
    handle_ = dlopen(lib_name_, flags);
    ONLD_TRACE("(" << lib_name_ << " = " << handle_ << ")");
    if ((to_check_open_ == true) && (handle_ == NULL)) {
      fprintf(stderr, "roctracer: Loading '%s' failed, %s\n", lib_name_, dlerror());
      abort();
    }

    T::init(this);
  }

  ~BaseLoader() {
    if (handle_ != NULL) dlclose(handle_);
  }

  static bool to_load_;
  static bool to_check_open_;
  static bool to_check_symb_;

  static mutex_t mutex_;
  static const char* lib_name_;
  static std::atomic<loader_t*> instance_;
  void* handle_;
};

// 'rocprofiler' library loader class
class RocpApi {
  public:
  typedef BaseLoader<RocpApi> Loader;

  typedef bool (RegisterCallback_t)(uint32_t op, void* callback, void* arg);
  typedef bool (OperateCallback_t)(uint32_t op);
  typedef bool (InitCallback_t)(void* callback, void* arg);
  typedef bool (EnableCallback_t)(uint32_t op, bool enable);
  typedef const char* (NameCallback_t)(uint32_t op);

  RegisterCallback_t* RegisterApiCallback;
  OperateCallback_t* RemoveApiCallback;
  InitCallback_t* InitActivityCallback;
  EnableCallback_t* EnableActivityCallback;
  NameCallback_t* GetOpName;

  RegisterCallback_t* RegisterEvtCallback;
  OperateCallback_t* RemoveEvtCallback;
  NameCallback_t* GetEvtName;

  protected:
  void init(Loader* loader) {
    RegisterApiCallback = loader->GetFun<RegisterCallback_t>("RegisterApiCallback");
    RemoveApiCallback = loader->GetFun<OperateCallback_t>("RemoveApiCallback");
    InitActivityCallback = loader->GetFun<InitCallback_t>("InitActivityCallback");
    EnableActivityCallback = loader->GetFun<EnableCallback_t>("EnableActivityCallback");
    GetOpName = loader->GetFun<NameCallback_t>("GetOpName");

    RegisterEvtCallback = loader->GetFun<RegisterCallback_t>("RegisterEvtCallback");
    RemoveEvtCallback = loader->GetFun<OperateCallback_t>("RemoveEvtCallback");
    GetEvtName = loader->GetFun<NameCallback_t>("GetEvtName");
  }
};

// HIP runtime library loader class
class HipApi {
  public:
  typedef BaseLoader<HipApi> Loader;

  typedef decltype(hipRegisterApiCallback) RegisterApiCallback_t;
  typedef decltype(hipRemoveApiCallback) RemoveApiCallback_t;
  typedef decltype(hipRegisterActivityCallback) RegisterActivityCallback_t;
  typedef decltype(hipRemoveActivityCallback) RemoveActivityCallback_t;
  typedef decltype(hipKernelNameRef) KernelNameRef_t;
  typedef decltype(hipKernelNameRefByPtr) KernelNameRefByPtr_t;
  typedef decltype(hipGetStreamDeviceId) GetStreamDeviceId_t;
  typedef decltype(hipApiName) ApiName_t;

  RegisterApiCallback_t* RegisterApiCallback;
  RemoveApiCallback_t* RemoveApiCallback;
  RegisterActivityCallback_t* RegisterActivityCallback;
  RemoveActivityCallback_t* RemoveActivityCallback;
  KernelNameRef_t* KernelNameRef;
  KernelNameRefByPtr_t* KernelNameRefByPtr;
  GetStreamDeviceId_t* GetStreamDeviceId;
  ApiName_t* ApiName;

  protected:
  void init(Loader* loader) {
    RegisterApiCallback = loader->GetFun<RegisterApiCallback_t>("hipRegisterApiCallback");
    RemoveApiCallback = loader->GetFun<RemoveApiCallback_t>("hipRemoveApiCallback");
    RegisterActivityCallback = loader->GetFun<RegisterActivityCallback_t>("hipRegisterActivityCallback");
    RemoveActivityCallback = loader->GetFun<RemoveActivityCallback_t>("hipRemoveActivityCallback");
    KernelNameRef = loader->GetFun<KernelNameRef_t>("hipKernelNameRef");
    KernelNameRefByPtr = loader->GetFun<KernelNameRefByPtr_t>("hipKernelNameRefByPtr");
    GetStreamDeviceId =  loader->GetFun<GetStreamDeviceId_t>("hipGetStreamDeviceId");
    ApiName = loader->GetFun<ApiName_t>("hipApiName");
  }
};

// HCC runtime library loader class
#include "inc/roctracer_hcc.h"
class HccApi {
  public:
  typedef BaseLoader<HccApi> Loader;

  hipInitAsyncActivityCallback_t* InitActivityCallback;
  hipEnableAsyncActivityCallback_t* EnableActivityCallback;
  hipGetOpName_t* GetOpName;

  protected:
  void init(Loader* loader) {
#if HIP_VDI
    InitActivityCallback = loader->GetFun<hipInitAsyncActivityCallback_t>("hipInitActivityCallback");
    EnableActivityCallback = loader->GetFun<hipEnableAsyncActivityCallback_t>("hipEnableActivityCallback");
    GetOpName = loader->GetFun<hipGetOpName_t>("hipGetCmdName");
#else
    InitActivityCallback = loader->GetFun<hipInitAsyncActivityCallback_t>("InitActivityCallbackImpl");
    EnableActivityCallback = loader->GetFun<hipEnableAsyncActivityCallback_t>("EnableActivityCallbackImpl");
    GetOpName = loader->GetFun<hipGetOpName_t>("GetCmdNameImpl");
#endif
  }
};

// KFD runtime library loader class
class KfdApi {
  public:
  typedef BaseLoader<KfdApi> Loader;

  typedef bool (RegisterApiCallback_t)(uint32_t op, void* callback, void* arg);
  typedef bool (RemoveApiCallback_t)(uint32_t op);

  RegisterApiCallback_t* RegisterApiCallback;
  RemoveApiCallback_t* RemoveApiCallback;

  protected:
  void init(Loader* loader) {
    RegisterApiCallback = loader->GetFun<RegisterApiCallback_t>("RegisterApiCallback");
    RemoveApiCallback = loader->GetFun<RemoveApiCallback_t>("RemoveApiCallback");
  }
};

// rocTX runtime library loader class
#include "inc/roctracer_roctx.h"
class RocTxApi {
  public:
  typedef BaseLoader<RocTxApi> Loader;

  typedef decltype(RegisterApiCallback) RegisterApiCallback_t;
  typedef decltype(RemoveApiCallback) RemoveApiCallback_t;
  typedef decltype(RangeStackIterate) RangeStackIterate_t;

  RegisterApiCallback_t* RegisterApiCallback;
  RemoveApiCallback_t* RemoveApiCallback;
  RangeStackIterate_t* RangeStackIterate;

  protected:
  void init(Loader* loader) {
    RegisterApiCallback = loader->GetFun<RegisterApiCallback_t>("RegisterApiCallback");
    RemoveApiCallback = loader->GetFun<RemoveApiCallback_t>("RemoveApiCallback");
    RangeStackIterate = loader->GetFun<RangeStackIterate_t>("RangeStackIterate");
  }
};

typedef BaseLoader<RocpApi> RocpLoader;
typedef BaseLoader<HipApi> HipLoader;
typedef BaseLoader<HccApi> HccLoader;
typedef BaseLoader<KfdApi> KfdLoader;
typedef BaseLoader<RocTxApi> RocTxLoader;

} // namespace roctracer

#define LOADER_INSTANTIATE2(HIP_LIB, HCC_LIB) \
  template<class T> typename roctracer::BaseLoader<T>::mutex_t roctracer::BaseLoader<T>::mutex_; \
  template<class T> std::atomic<roctracer::BaseLoader<T>*> roctracer::BaseLoader<T>::instance_{}; \
  template<class T> bool roctracer::BaseLoader<T>::to_load_ = false; \
  template<class T> bool roctracer::BaseLoader<T>::to_check_open_ = true; \
  template<class T> bool roctracer::BaseLoader<T>::to_check_symb_ = true; \
  template<> const char* roctracer::RocpLoader::lib_name_ = "librocprofiler64.so"; \
  template<> bool roctracer::RocpLoader::to_load_ = true; \
  template<> const char* roctracer::HipLoader::lib_name_ = HIP_LIB; \
  template<> bool roctracer::HipLoader::to_check_open_ = false; \
  template<> const char* roctracer::HccLoader::lib_name_ = HCC_LIB; \
  template<> bool roctracer::HccLoader::to_check_open_ = false; \
  template<> const char* roctracer::KfdLoader::lib_name_ = "libkfdwrapper64.so"; \
  template<> const char* roctracer::RocTxLoader::lib_name_ = "libroctx64.so"; \
  template<> bool roctracer::RocTxLoader::to_load_ = true;

#if HIP_VDI
#define LOADER_INSTANTIATE() LOADER_INSTANTIATE2("libamdhip64.so", "libamdhip64.so");
#else
#define LOADER_INSTANTIATE() LOADER_INSTANTIATE2("libhip_hcc.so", "libmcwamp.so");
#endif

#endif // SRC_CORE_LOADER_H_
