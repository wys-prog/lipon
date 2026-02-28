#pragma once

#include <string>
#include <stdexcept>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace lipon {
  class lipon_DynamicLibrary {
  public:
    inline lipon_DynamicLibrary() = default;

    inline explicit lipon_DynamicLibrary(const std::string& path) {
      open(path);
    }

    inline ~lipon_DynamicLibrary() {
      close();
    }

    inline lipon_DynamicLibrary(const lipon_DynamicLibrary&) = delete;
    inline lipon_DynamicLibrary& operator=(const lipon_DynamicLibrary&) = delete;

    inline lipon_DynamicLibrary(lipon_DynamicLibrary&& other) noexcept
      : handle(other.handle) {
      other.handle = nullptr;
    }

    inline lipon_DynamicLibrary& operator=(lipon_DynamicLibrary&& other) noexcept {
      if (this != &other) {
        close();
        handle = other.handle;
        other.handle = nullptr;
      }
      return *this;
    }

    inline void open(const std::string& path) {
      close();

  #if defined(_WIN32)
      handle = LoadLibraryA(path.c_str());
      if (!handle)
        throw std::runtime_error("Failed to load library: " + path);
  #else
      handle = dlopen(path.c_str(), RTLD_NOW);
      if (!handle)
        throw std::runtime_error(std::string("Failed to load library: ") + dlerror());
  #endif
    }

    inline void close() {
      if (!handle)
        return;

  #if defined(_WIN32)
      FreeLibrary(static_cast<HMODULE>(handle));
  #else
      dlclose(handle);
  #endif

      handle = nullptr;
    }

    template<typename T>
    inline T get_function(const std::string& name) {
      if (!handle) throw std::runtime_error("Library not loaded");

  #if defined(_WIN32)
      auto symbol = GetProcAddress(static_cast<HMODULE>(handle), name.c_str());
      if (!symbol)
        throw std::runtime_error("Failed to get function: " + name);
      return reinterpret_cast<T>(symbol);
  #else
      dlerror();
      auto symbol = dlsym(handle, name.c_str());
      const char* error = dlerror();
      if (error) throw std::runtime_error(std::string("Failed to get function: ") + error);
      return reinterpret_cast<T>(symbol);
  #endif
    }

    inline bool is_loaded() const {
      return handle != nullptr;
    }

  private:
    void* handle = nullptr;
  };
}