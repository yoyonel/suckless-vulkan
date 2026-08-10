#include "module_loader.h"
#include "app_log.h"
#include <dlfcn.h>

ModuleLoader::~ModuleLoader() {
    Unload();
}

ResourceResult ModuleLoader::Load(const std::string& path) {
    Unload();
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOG_ERROR("module", "Failed to load %s: %s", path.c_str(), dlerror());
        return ResourceResult::ErrorParseFailed;
    }
    return ResourceResult::Success;
}

void ModuleLoader::Unload() {
    if (handle) {
        dlclose(handle);
        handle = nullptr;
    }
}

void* ModuleLoader::GetSymbol(const std::string& name) const {
    if (!handle)
        return nullptr;
    return dlsym(handle, name.c_str());
}

bool ModuleLoader::IsLoaded() const {
    return handle != nullptr;
}
