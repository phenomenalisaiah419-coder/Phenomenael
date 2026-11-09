#include "core/client_process_runtime.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#if defined(__ANDROID__) || defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace bedrockai {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool valid_hex_build_id(const std::string& id) {
    if (id.empty() || (id.size() % 2U) != 0U) return false;
    for (const char raw : id) {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool hex = (c >= static_cast<unsigned char>('0') && c <= static_cast<unsigned char>('9')) ||
                         (c >= static_cast<unsigned char>('a') && c <= static_cast<unsigned char>('f')) ||
                         (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('F'));
        if (!hex) return false;
    }
    return true;
}

} // namespace

ClientProcessRuntime::~ClientProcessRuntime() {
    detach();
}

bool ClientProcessRuntime::prepare(const std::string& required_build_id_hex, std::string* error) {
    detach();
    if (!required_build_id_hex.empty() && !valid_hex_build_id(required_build_id_hex)) {
        set_error(error, "required build ID must be non-empty hexadecimal when supplied");
        return false;
    }
    if (!discover_loaded_library(error)) return false;
    if (!client_image_is_compatible(image_, error)) {
        detach();
        return false;
    }
    if (!required_build_id_hex.empty() && image_.build_id_hex != required_build_id_hex) {
        set_error(error, "loaded Bedrock build ID does not match required build ID");
        detach();
        return false;
    }
    prepared_ = true;
    return true;
}

bool ClientProcessRuntime::refresh(std::string* error) {
    if (!prepared_) {
        set_error(error, "client process runtime is not prepared");
        return false;
    }
    ClientImageInfo fresh;
    std::string inspect_error;
    if (!inspect_current_client_image(fresh, &inspect_error)) {
        set_error(error, inspect_error);
        return false;
    }
    if (fresh.build_id_hex != image_.build_id_hex || fresh.base != image_.base) {
        set_error(error, "loaded client image changed while runtime was prepared");
        return false;
    }
    image_ = std::move(fresh);
    return true;
}

void ClientProcessRuntime::detach() {
#if defined(__ANDROID__) || defined(__linux__)
    if (owned_handle_) {
        (void)dlclose(owned_handle_);
        owned_handle_ = nullptr;
    }
#endif
    image_ = ClientImageInfo{};
    process_id_ = 0U;
    prepared_ = false;
}

bool ClientProcessRuntime::discover_loaded_library(std::string* error) {
    if (!inspect_current_client_image(image_, error)) return false;
#if defined(__ANDROID__) || defined(__linux__)
    process_id_ = static_cast<std::uint64_t>(::getpid());
    // The client is normally already loaded. RTLD_NOLOAD prevents this helper from
    // introducing an unrelated copy of the game library into the process.
#if defined(RTLD_NOLOAD)
    owned_handle_ = ::dlopen(kEmbeddedClientLibrary, RTLD_NOW | RTLD_NOLOAD);
#else
    owned_handle_ = nullptr;
#endif
    if (!owned_handle_) {
        // dl_iterate_phdr already proved that the image is resident; dlsym can still
        // resolve exported symbols through the global namespace. We deliberately do
        // not dlopen a second copy here.
        return true;
    }
    return true;
#else
    set_error(error, "current client process runtime requires Android/Linux");
    return false;
#endif
}

void* ClientProcessRuntime::resolve_symbol(const char* symbol, std::string* error) const {
    if (!prepared_) {
        set_error(error, "client process runtime is not prepared");
        return nullptr;
    }
    if (!symbol || *symbol == '\0') {
        set_error(error, "symbol name is empty");
        return nullptr;
    }
#if defined(__ANDROID__) || defined(__linux__)
    ::dlerror();
    void* resolved = owned_handle_ ? ::dlsym(owned_handle_, symbol) : ::dlsym(RTLD_DEFAULT, symbol);
    const char* detail = ::dlerror();
    if (detail != nullptr || resolved == nullptr) {
        set_error(error, detail ? std::string("symbol resolution failed: ") + detail :
                                  "symbol resolution returned null");
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(resolved);
    if (!client_executable_contains(image_, address, 1U)) {
        set_error(error, "resolved symbol is outside libminecraftpe.so executable segments");
        return nullptr;
    }
    return resolved;
#else
    set_error(error, "symbol resolution requires Android/Linux");
    return nullptr;
#endif
}

std::string ClientProcessRuntime::report() const {
    std::string report = format_client_image_report(image_);
    report += "prepared=";
    report += prepared_ ? "true\n" : "false\n";
    report += "pid=" + std::to_string(process_id_) + "\n";
    return report;
}

} // namespace bedrockai

namespace {
void write_error_buffer(char* buffer, std::size_t size, const std::string& error) {
    if (!buffer || size == 0U) return;
    const int written = std::snprintf(buffer, size, "%s", error.c_str());
    if (written < 0) buffer[0] = '\0';
}
}


extern "C" {

std::uintptr_t bedrockai_process_runtime_create() {
    auto* runtime = new (std::nothrow) bedrockai::ClientProcessRuntime();
    return reinterpret_cast<std::uintptr_t>(runtime);
}

void bedrockai_process_runtime_destroy(std::uintptr_t handle) {
    delete reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
}

int bedrockai_process_runtime_prepare(std::uintptr_t handle, const char* required_build_id_hex,
                                      char* error_buffer, std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    if (!runtime) {
        write_error_buffer(error_buffer, error_buffer_size, "process runtime handle is null");
        return 0;
    }
    std::string error;
    const bool ok = runtime->prepare(required_build_id_hex ? required_build_id_hex : "", &error);
    write_error_buffer(error_buffer, error_buffer_size, error);
    return ok ? 1 : 0;
}

int bedrockai_process_runtime_refresh(std::uintptr_t handle, char* error_buffer, std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    if (!runtime) {
        write_error_buffer(error_buffer, error_buffer_size, "process runtime handle is null");
        return 0;
    }
    std::string error;
    const bool ok = runtime->refresh(&error);
    write_error_buffer(error_buffer, error_buffer_size, error);
    return ok ? 1 : 0;
}

void bedrockai_process_runtime_detach(std::uintptr_t handle) {
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    if (runtime) runtime->detach();
}

int bedrockai_process_runtime_prepared(std::uintptr_t handle) {
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    return runtime && runtime->prepared() ? 1 : 0;
}

std::uint64_t bedrockai_process_runtime_pid(std::uintptr_t handle) {
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    return runtime ? runtime->process_id() : 0U;
}

int bedrockai_process_runtime_report(std::uintptr_t handle, char* buffer, std::size_t buffer_size) {
    if (!buffer || buffer_size == 0U) return 0;
    auto* runtime = reinterpret_cast<bedrockai::ClientProcessRuntime*>(handle);
    if (!runtime) {
        write_error_buffer(buffer, buffer_size, "process runtime handle is null");
        return 0;
    }
    const std::string report = runtime->report();
    if (report.size() + 1U > buffer_size) {
        buffer[0] = '\0';
        return 0;
    }
    std::memcpy(buffer, report.c_str(), report.size() + 1U);
    return 1;
}

} // extern "C"
