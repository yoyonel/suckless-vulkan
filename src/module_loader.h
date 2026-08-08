#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include <string>

class ModuleLoader {
  public:
    ModuleLoader() = default;
    ~ModuleLoader();

    bool Load(const std::string& path);
    void Unload();
    void* GetSymbol(const std::string& name) const;
    bool IsLoaded() const;

  private:
    void* handle = nullptr;
};

#endif
