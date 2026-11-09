#pragma once

#include "bedrock_client_integration.hpp"

#include <cstdint>
#include <string>

namespace bedrockai {

class ClientProcessRuntime {
public:
    ClientProcessRuntime() = default;
    ~ClientProcessRuntime();
    ClientProcessRuntime(const ClientProcessRuntime&) = delete;
    ClientProcessRuntime& operator=(const ClientProcessRuntime&) = delete;

    bool prepare(const std::string& required_build_id_hex = {}, std::string* error = nullptr);
    bool refresh(std::string* error = nullptr);
    void detach();

    bool prepared() const { return prepared_; }
    const ClientImageInfo& image() const { return image_; }
    std::uint64_t process_id() const { return process_id_; }

    void* resolve_symbol(const char* symbol, std::string* error = nullptr) const;
    std::string report() const;

private:
    bool discover_loaded_library(std::string* error);

    ClientImageInfo image_{};
    std::uint64_t process_id_ = 0;
    void* owned_handle_ = nullptr;
    bool prepared_ = false;
};

} // namespace bedrockai

extern "C" {
std::uintptr_t bedrockai_process_runtime_create();
void bedrockai_process_runtime_destroy(std::uintptr_t handle);
int bedrockai_process_runtime_prepare(std::uintptr_t handle,
                                      const char* required_build_id_hex,
                                      char* error_buffer,
                                      std::size_t error_buffer_size);
int bedrockai_process_runtime_refresh(std::uintptr_t handle,
                                      char* error_buffer,
                                      std::size_t error_buffer_size);
void bedrockai_process_runtime_detach(std::uintptr_t handle);
int bedrockai_process_runtime_prepared(std::uintptr_t handle);
std::uint64_t bedrockai_process_runtime_pid(std::uintptr_t handle);
int bedrockai_process_runtime_report(std::uintptr_t handle,
                                     char* buffer,
                                     std::size_t buffer_size);
}
