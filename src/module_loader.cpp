#include "module_loader.h"
#include "app_log.h"
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

static std::string get_executable_dir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        std::string path(buf);
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos + 1);
        }
    }
    return "./";
}

ModuleLoader::~ModuleLoader() {
    Unload();
}

ResourceResult ModuleLoader::Load(const std::string& path) {
    Unload();

    std::string full_path = path;
    if (!path.empty() && path[0] != '/') {
        full_path = get_executable_dir() + path;
    }

#ifdef TRACY_ENABLE
    handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#else
    handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle) {
        LOG_ERROR("module", "Failed to load %s: %s", full_path.c_str(), dlerror());
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
