#include "core/bedrock_client_integration.hpp"
#include "core/hook_plan.hpp"
#include "core/client_hook_dispatch.hpp"
#include "core/client_abi_adapter.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#if defined(__ANDROID__) || defined(__linux__)
#include <elf.h>
#include <link.h>
#endif
#include <new>
#include <thread>
#include <utility>

namespace bedrockai {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

struct ImageSearch {
    const char* wanted = nullptr;
    ClientImageInfo* out = nullptr;
    bool found = false;
};

bool align4(std::size_t value, std::size_t& out) {
    if (value > std::numeric_limits<std::size_t>::max() - 3U) return false;
    out = (value + 3U) & ~std::size_t{3U};
    return true;
}

bool read_build_id(const std::uint8_t* notes, std::size_t size, std::string& out) {
    out.clear();
    std::size_t offset = 0;
    while (offset + sizeof(Elf64_Nhdr) <= size) {
        Elf64_Nhdr note{};
        std::memcpy(&note, notes + offset, sizeof(note));
        const std::size_t name_size = static_cast<std::size_t>(note.n_namesz);
        const std::size_t desc_size = static_cast<std::size_t>(note.n_descsz);
        std::size_t name_padded = 0;
        std::size_t desc_padded = 0;
        if (!align4(name_size, name_padded) || !align4(desc_size, desc_padded)) return false;
        if (name_size > size - offset - sizeof(note) || name_padded > size - offset - sizeof(note)) return false;
        const std::size_t after_name = offset + sizeof(note) + name_padded;
        if (after_name > size || desc_size > size - after_name) return false;
        const std::size_t after_desc = after_name + desc_padded;
        if (after_desc > size) return false;
        const auto* name = notes + offset + sizeof(note);
        const auto* desc = notes + after_name;
        if (note.n_type == NT_GNU_BUILD_ID && name_size >= 4U &&
            std::memcmp(name, "GNU", 4U) == 0 && desc_size > 0U) {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (std::size_t i = 0; i < desc_size; ++i) {
                stream << std::setw(2) << static_cast<unsigned int>(desc[i]);
            }
            out = stream.str();
            return true;
        }
        offset = after_desc;
    }
    return false;
}

int visit_image(struct dl_phdr_info* info, std::size_t, void* opaque) {
    auto* search = static_cast<ImageSearch*>(opaque);
    const char* name = info->dlpi_name;
    if (!name || !*name || !search->wanted) return 0;
    const char* base = std::strrchr(name, '/');
    base = base ? base + 1 : name;
    if (std::string(base) != search->wanted) return 0;

    std::uintptr_t min_addr = std::numeric_limits<std::uintptr_t>::max();
    std::uintptr_t max_addr = 0;
    search->out->executable_segments.clear();
    for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - info->dlpi_addr) return 0;
        const auto start = info->dlpi_addr + ph.p_vaddr;
        if (ph.p_memsz > std::numeric_limits<std::uintptr_t>::max() - start) return 0;
        const auto end = start + ph.p_memsz;
        min_addr = std::min(min_addr, static_cast<std::uintptr_t>(start));
        max_addr = std::max(max_addr, static_cast<std::uintptr_t>(end));
        if ((ph.p_flags & PF_X) != 0U && ph.p_memsz != 0U) {
            search->out->executable_segments.push_back(ClientExecutableSegment{
                static_cast<std::uintptr_t>(start), static_cast<std::size_t>(ph.p_memsz), ph.p_flags});
        }
    }
    if (min_addr == std::numeric_limits<std::uintptr_t>::max() || max_addr <= min_addr) return 0;
    search->out->base = min_addr;
    search->out->mapped_size = static_cast<std::size_t>(max_addr - min_addr);
    search->out->library_name = base;
    for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE || ph.p_memsz == 0U) continue;
        const auto note_addr = info->dlpi_addr + ph.p_vaddr;
        const auto note_size = static_cast<std::size_t>(ph.p_memsz);
        if (read_build_id(reinterpret_cast<const std::uint8_t*>(note_addr), note_size, search->out->build_id_hex)) break;
    }
    // Validate the loaded ELF header directly so the binder can distinguish wrong-architecture libraries.
    const auto* elf = reinterpret_cast<const std::uint8_t*>(info->dlpi_addr);
    if (elf[EI_MAG0] == ELFMAG0 && elf[EI_MAG1] == ELFMAG1 && elf[EI_MAG2] == ELFMAG2 && elf[EI_MAG3] == ELFMAG3) {
        Elf64_Ehdr header{};
        std::memcpy(&header, elf, sizeof(header));
        search->out->elf_class = header.e_ident[EI_CLASS];
        search->out->machine = header.e_machine;
    }
    search->found = true;
    return 1;
}

bool validate_text_field(const char* value, const char* expected, const char* field, std::string* error) {
    if (!value || std::string(value) != expected) {
        set_error(error, std::string(field) + " mismatch");
        return false;
    }
    return true;
}

} // namespace

bool inspect_current_client_image(ClientImageInfo& out, std::string* error) {
    out = ClientImageInfo{};
#if defined(__ANDROID__) || defined(__linux__)
    ImageSearch search{kEmbeddedClientLibrary, &out, false};
    dl_iterate_phdr(visit_image, &search);
    if (!search.found) {
        set_error(error, "libminecraftpe.so is not loaded in this process");
        return false;
    }
    return true;
#else
    set_error(error, "current client image inspection is unsupported on this host");
    return false;
#endif
}

bool client_image_is_compatible(const ClientImageInfo& image, std::string* error) {
    if (image.library_name != kEmbeddedClientLibrary) { set_error(error, "wrong client library"); return false; }
    if (image.elf_class != 2U || image.machine != 183U) { set_error(error, "client image is not ELF64 AArch64"); return false; }
    if (image.base == 0U || image.mapped_size == 0U || image.executable_segments.empty()) { set_error(error, "client image has no valid executable mapping"); return false; }
    if (image.build_id_hex.empty()) { set_error(error, "client image has no GNU build ID"); return false; }
    return true;
}

std::string format_client_image_report(const ClientImageInfo& image) {
    std::ostringstream stream;
    stream << "library=" << image.library_name << '\n';
    stream << "base=0x" << std::hex << image.base << std::dec << '\n';
    stream << "mapped_size=" << image.mapped_size << '\n';
    stream << "elf_class=" << static_cast<unsigned int>(image.elf_class * 8U) << '\n';
    stream << "machine=" << image.machine << '\n';
    stream << "build_id=" << image.build_id_hex << '\n';
    stream << "executable_segments=" << image.executable_segments.size() << '\n';
    for (std::size_t i = 0; i < image.executable_segments.size(); ++i) {
        const auto& segment = image.executable_segments[i];
        stream << "exec" << i << "=0x" << std::hex << segment.start << std::dec
               << "," << segment.size << ",flags=" << segment.flags << '\n';
    }
    return stream.str();
}

EmbeddedBedrockClientIntegration::EmbeddedBedrockClientIntegration() = default;

EmbeddedBedrockClientIntegration::~EmbeddedBedrockClientIntegration() {
    detach();
}

bool EmbeddedBedrockClientIntegration::attach(const EmbeddedClientHostApi& api, std::string* error) {
    if (tick_hook_.installed) {
        set_error(error, "embedded client cannot be reattached after a live Tick hook was installed");
        return false;
    }
    detach();
    if (api.abi_version != kEmbeddedClientAbiVersion) {
        set_error(error, "embedded client ABI version mismatch");
        return false;
    }
    if (api.protocol != kEmbeddedClientProtocol) {
        set_error(error, "embedded client protocol mismatch");
        return false;
    }
    if (!validate_text_field(api.game_version, kEmbeddedClientGameVersion, "game version", error)) return false;
    if (!validate_text_field(api.library_name, kEmbeddedClientLibrary, "library name", error)) return false;
    if (!api.read_client_state || !api.read_inventory || !api.read_touch || !api.compute_ai || !api.submit_actions) {
        set_error(error, "required client host callbacks are missing");
        return false;
    }

    api_ = api;
    if (api_.get_image_info) {
        if (api_.get_image_info(api_.user, &image_info_) == 0) {
            set_error(error, "client host failed to provide image information");
            detach();
            return false;
        }
    } else if (!inspect_current_client_image(image_info_, error)) {
        detach();
        return false;
    }
    if (!client_image_is_compatible(image_info_, error)) {
        detach();
        return false;
    }
    attached_ = true;
    return true;
}

void EmbeddedBedrockClientIntegration::detach() {
    hook_dispatch_state_.fetch_or(kHookDispatchDisabledBit, std::memory_order_acq_rel);
    while ((hook_dispatch_state_.load(std::memory_order_acquire) & ~kHookDispatchDisabledBit) != 0U) {
        std::this_thread::yield();
    }
    if (tick_hook_.context) {
        tick_hook_.context->dispatch.store(nullptr, std::memory_order_release);
        tick_hook_.context->dispatch_user.store(nullptr, std::memory_order_release);
        while (tick_hook_.context->in_flight.load(std::memory_order_acquire) != 0U) {
            std::this_thread::yield();
        }
    }
    api_ = EmbeddedClientHostApi{};
    image_info_ = ClientImageInfo{};
    last_actions_ = EmbeddedActionFrame{};
    last_error_.clear();
    tick_count_.store(0U, std::memory_order_release);
    verified_bindings_.clear();
    hook_kind_installed_.fill(false);
    hook_dispatch_registered_ = false;
    native_abi_certificate_ = ClientNativeAbiCertificate{};
    hook_dispatcher_.reset();
    attached_ = false;
}

bool EmbeddedBedrockClientIntegration::copy_state(const ClientStateView& client,
                                                   WorldSnapshot& snapshot,
                                                   std::string* error) {
    if (client.player.health < 0.0) {
        set_error(error, "client supplied negative player health");
        return false;
    }
    if (client.entity_count > 4096 || client.block_count > 16384) {
        set_error(error, "client state collection exceeds embedded safety limit");
        return false;
    }
    if ((client.entity_count != 0 && !client.entities) || (client.block_count != 0 && !client.blocks)) {
        set_error(error, "client state collection pointer is missing");
        return false;
    }
    snapshot = WorldSnapshot{};
    snapshot.tick = client.tick;
    snapshot.player = client.player;
    snapshot.last_correction = client.last_correction;
    snapshot.joined = client.joined;
    snapshot.world.dimension = client.dimension;
    for (std::size_t i = 0; i < client.entity_count; ++i) {
        const auto& e = client.entities[i];
        EntityState value;
        value.runtime_id = e.runtime_id;
        value.type_id = e.type_id;
        value.position = e.position;
        value.velocity = e.velocity;
        value.health = e.health;
        value.hostile = e.hostile;
        value.alive = e.alive;
        snapshot.world.entities.emplace(value.runtime_id, value);
    }
    for (std::size_t i = 0; i < client.block_count; ++i) {
        const auto& b = client.blocks[i];
        BlockObservation value;
        value.x = b.x;
        value.y = b.y;
        value.z = b.z;
        value.runtime_id = b.runtime_id;
        value.solid = b.solid;
        value.replaceable = b.replaceable;
        value.observed_tick = client.tick;
        snapshot.world.blocks.emplace(block_key(value.x, value.y, value.z), value);
    }
    return true;
}

bool EmbeddedBedrockClientIntegration::tick(std::string* error) {
    if (!attached_) {
        set_error(error, "embedded client is not attached");
        return false;
    }
    ClientStateView client;
    ClientInventoryView inventory_view;
    EmbeddedTouchInput touch;
    if (api_.read_client_state(api_.user, &client) == 0) {
        set_error(error, "client state callback failed");
        return false;
    }
    if (api_.read_inventory(api_.user, &inventory_view) == 0) {
        set_error(error, "inventory callback failed");
        return false;
    }
    if (api_.read_touch(api_.user, &touch) == 0) {
        set_error(error, "touch callback failed");
        return false;
    }

    WorldSnapshot snapshot;
    if (!copy_state(client, snapshot, error)) return false;
    EmbeddedAiInput ai;
    if (api_.compute_ai(api_.user, &snapshot, &inventory_view.inventory, &ai) == 0) {
        set_error(error, "AI decision callback failed");
        return false;
    }
    EmbeddedActionFrame output;
    std::string bridge_error;
    if (!bridge_.tick(snapshot, inventory_view.inventory, touch, ai, output, &bridge_error)) {
        set_error(error, bridge_error);
        return false;
    }
    if (api_.submit_actions(api_.user, &output) == 0) {
        set_error(error, "client action callback failed");
        return false;
    }
    last_actions_ = output;
    tick_count_.fetch_add(1U, std::memory_order_acq_rel);
    last_error_.clear();
    return true;
}

void EmbeddedBedrockClientIntegration::tick_thunk(void* user) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user);
    if (!self) return;
    self->tick_from_host();
}

void EmbeddedBedrockClientIntegration::tick_dispatch(void* user, Arm64RegisterFrame*) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user);
    if (!self) return;
    self->tick_from_host();
}

void EmbeddedBedrockClientIntegration::tick_from_host() {
    std::string error;
    if (!tick(&error)) last_error_ = error;
}

bool EmbeddedBedrockClientIntegration::bind_verified_hooks(const std::vector<ClientHookSpec>& specs,
                                                             ClientHookInstaller installer,
                                                             void* user,
                                                             std::string* error) {
    if (!attached_) {
        set_error(error, "embedded client is not attached");
        return false;
    }
    if (image_info_.library_name != kEmbeddedClientLibrary || image_info_.base == 0U) {
        set_error(error, "embedded client image is not a valid Bedrock image");
        return false;
    }
    return install_verified_client_hooks(image_info_, specs, installer, user, error);
}

bool EmbeddedBedrockClientIntegration::install_verified_tick_hook(const ClientHookSpec& spec, std::string* error) {
    if (!attached_) {
        set_error(error, "embedded client is not attached");
        return false;
    }
    if (tick_hook_.installed) {
        set_error(error, "a live Tick hook is already installed");
        return false;
    }
    if (spec.kind != ClientHookKind::Tick) {
        set_error(error, "client Tick hook specification has the wrong kind");
        return false;
    }
    Arm64HookBinder binder;
    std::vector<ClientHookBinding> bindings;
    if (!binder.bind(image_info_, {spec}, bindings, error)) return false;
    if (bindings.size() != 1U) {
        set_error(error, "Tick hook binding did not produce exactly one binding");
        return false;
    }
    return hook_runtime_.install_tick(image_info_, spec, bindings.front(), this,
                                      &EmbeddedBedrockClientIntegration::tick_dispatch,
                                      tick_hook_, error);
}

bool EmbeddedBedrockClientIntegration::install_complete_hook_plan(const std::vector<ClientHookSpec>& specs,
                                                                    std::string* error) {
    if (!attached_) {
        set_error(error, "embedded client is not attached");
        return false;
    }
    if (!api_.install_client_hooks) {
        set_error(error, "client does not expose the exact-ABI hook installer callback");
        return false;
    }
    if (!validate_complete_client_hook_plan(specs, image_info_.build_id_hex, error)) return false;
    if (!verified_bindings_.empty()) {
        set_error(error, "verified client hook set is already installed");
        return false;
    }
    Arm64HookBinder binder;
    if (!binder.bind(image_info_, specs, verified_bindings_, error)) return false;
    if (!api_.register_hook_dispatch) {
        set_error(error, "client does not expose the translated hook-dispatch registration ABI");
        return false;
    }
    if (!api_.get_native_abi_certificate) {
        set_error(error, "client does not expose the native ABI certificate reader");
        return false;
    }
    if (!api_.tick_identity) {
        set_error(error, "client does not expose the exact original Tick identity");
        return false;
    }

    ClientAbiTable table;
    table.client_context = api_.user;
    table.tick = api_.tick_identity;
    table.read_state = api_.read_client_state;
    table.read_inventory = api_.read_inventory;
    table.read_touch = api_.read_touch;
    table.submit_actions = api_.submit_actions;
    hook_dispatcher_ = std::make_unique<ClientHookDispatcher>();
    const auto compute = [this](const WorldSnapshot& snapshot, const InventoryState& inventory, EmbeddedAiInput& out, std::string* local_error) {
        if (!api_.compute_ai(api_.user, &snapshot, &inventory, &out)) {
            if (local_error) *local_error = "AI decision callback failed";
            return false;
        }
        return true;
    };
    if (!hook_dispatcher_->attach(table, bridge_, compute, error)) {
        hook_dispatcher_.reset();
        return false;
    }
    if (!validate_native_abi_certificate(error)) {
        hook_dispatcher_.reset();
        return false;
    }
    if (!register_hook_dispatch(error)) {
        hook_dispatcher_.reset();
        return false;
    }
    if (api_.install_client_hooks(api_.user, verified_bindings_.data(), verified_bindings_.size()) == 0) {
        verified_bindings_.clear();
        hook_dispatch_registered_ = false;
        set_error(error, "client rejected verified hook set");
        return false;
    }
    hook_kind_installed_.fill(false);
    for (const auto& binding : verified_bindings_) {
        const auto index = static_cast<std::size_t>(binding.kind);
        if (index < hook_kind_installed_.size()) hook_kind_installed_[index] = true;
    }
    return true;
}

int EmbeddedBedrockClientIntegration::hook_dispatch_tick(void* user, const ClientHookEventMetadata* metadata) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user);
    if (!self || !self->enter_hook_dispatch()) return 0;
    if (!self->hook_dispatcher_) { self->leave_hook_dispatch(); return 0; }
    std::string error;
    const int ok = self->hook_dispatcher_->dispatch_tick(metadata, &error);
    if (!ok) self->last_error_ = error;
    if (ok) {
        self->last_actions_ = self->hook_dispatcher_->last_actions();
        self->tick_count_.fetch_add(1U, std::memory_order_acq_rel);
    }
    self->leave_hook_dispatch();
    return ok;
}

int EmbeddedBedrockClientIntegration::hook_dispatch_state(void* user, const ClientHookEventMetadata* metadata, const ClientStateView* state) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user); if (!self || !self->enter_hook_dispatch()) return 0;
    if (!self->hook_dispatcher_) { self->leave_hook_dispatch(); return 0; }
    std::string error; const int ok = self->hook_dispatcher_->dispatch_read_state(metadata, state, &error); if (!ok) self->last_error_ = error; self->leave_hook_dispatch(); return ok;
}
int EmbeddedBedrockClientIntegration::hook_dispatch_inventory(void* user, const ClientHookEventMetadata* metadata, const ClientInventoryView* inventory) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user); if (!self || !self->enter_hook_dispatch()) return 0;
    if (!self->hook_dispatcher_) { self->leave_hook_dispatch(); return 0; }
    std::string error; const int ok = self->hook_dispatcher_->dispatch_read_inventory(metadata, inventory, &error); if (!ok) self->last_error_ = error; self->leave_hook_dispatch(); return ok;
}
int EmbeddedBedrockClientIntegration::hook_dispatch_touch(void* user, const ClientHookEventMetadata* metadata, const EmbeddedTouchInput* touch) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user); if (!self || !self->enter_hook_dispatch()) return 0;
    if (!self->hook_dispatcher_) { self->leave_hook_dispatch(); return 0; }
    std::string error; const int ok = self->hook_dispatcher_->dispatch_read_touch(metadata, touch, &error); if (!ok) self->last_error_ = error; self->leave_hook_dispatch(); return ok;
}
int EmbeddedBedrockClientIntegration::hook_dispatch_actions(void* user, const ClientHookEventMetadata* metadata, const EmbeddedActionFrame* actions) {
    auto* self = static_cast<EmbeddedBedrockClientIntegration*>(user); if (!self || !self->enter_hook_dispatch()) return 0;
    if (!self->hook_dispatcher_) { self->leave_hook_dispatch(); return 0; }
    std::string error; const int ok = self->hook_dispatcher_->dispatch_submit_actions(metadata, actions, &error); if (!ok) self->last_error_ = error; self->leave_hook_dispatch(); return ok;
}

bool EmbeddedBedrockClientIntegration::validate_native_abi_certificate(std::string* error) {
    native_abi_certificate_ = ClientNativeAbiCertificate{};
    if (api_.get_native_abi_certificate(api_.user, &native_abi_certificate_, error) == 0) {
        if (error && !error->empty()) return false;
        set_error(error, "client native ABI certificate reader failed");
        return false;
    }
    ClientNativeAbiValidator validator;
    if (!validator.validate(image_info_, verified_bindings_, native_abi_certificate_, error)) {
        return false;
    }
    return true;
}

bool EmbeddedBedrockClientIntegration::register_hook_dispatch(std::string* error) {
    ClientHookDispatchRegistration registration;
    registration.user = this;
    registration.on_tick = &EmbeddedBedrockClientIntegration::hook_dispatch_tick;
    registration.on_read_state = &EmbeddedBedrockClientIntegration::hook_dispatch_state;
    registration.on_read_inventory = &EmbeddedBedrockClientIntegration::hook_dispatch_inventory;
    registration.on_read_touch = &EmbeddedBedrockClientIntegration::hook_dispatch_touch;
    registration.on_submit_actions = &EmbeddedBedrockClientIntegration::hook_dispatch_actions;
    if (api_.register_hook_dispatch(api_.user, &registration, error) == 0) {
        set_error(error, error && !error->empty() ? *error : "client rejected translated hook-dispatch registration");
        return false;
    }
    hook_dispatch_registered_ = true;
    hook_dispatcher_->require_coherent_native_frames();
    hook_dispatch_state_.store(0U, std::memory_order_release);
    return true;
}

bool EmbeddedBedrockClientIntegration::enter_hook_dispatch() {
    auto state = hook_dispatch_state_.load(std::memory_order_acquire);
    for (;;) {
        if ((state & kHookDispatchDisabledBit) != 0U) return false;
        const auto count = state & ~kHookDispatchDisabledBit;
        if (count >= kHookDispatchDisabledBit - 1U) return false;
        if (hook_dispatch_state_.compare_exchange_weak(state, state + 1U,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) return true;
    }
}

void EmbeddedBedrockClientIntegration::leave_hook_dispatch() {
    hook_dispatch_state_.fetch_sub(1U, std::memory_order_acq_rel);
}

bool EmbeddedBedrockClientIntegration::hook_kind_installed(ClientHookKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    return index < hook_kind_installed_.size() && hook_kind_installed_[index];
}

bool EmbeddedBedrockClientIntegration::install_tick_callback(std::string* error) {
    if (!attached_) {
        set_error(error, "embedded client is not attached");
        return false;
    }
    if (!api_.register_tick_callback) {
        set_error(error, "client does not expose a tick registration callback");
        return false;
    }
    if (api_.register_tick_callback(api_.user, &EmbeddedBedrockClientIntegration::tick_thunk, this) == 0) {
        set_error(error, "client tick registration failed");
        return false;
    }
    return true;
}

} // namespace bedrockai

extern "C" {

namespace {
void copy_error(const std::string& value, char* buffer, std::size_t size) {
    if (!buffer || size == 0) return;
    const std::size_t count = std::min(size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}
}

std::uintptr_t bedrockai_client_integration_create() {
    auto* runtime = new (std::nothrow) bedrockai::EmbeddedBedrockClientIntegration();
    return reinterpret_cast<std::uintptr_t>(runtime);
}

void bedrockai_client_integration_destroy(std::uintptr_t handle) {
    delete reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
}

int bedrockai_client_integration_attach(std::uintptr_t handle,
                                         const BedrockAiEmbeddedClientHost* host,
                                         char* error_buffer,
                                         std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime || !host) { copy_error("invalid integration or host handle", error_buffer, error_buffer_size); return 0; }
    bedrockai::EmbeddedClientHostApi api;
    api.abi_version = host->abi_version;
    api.protocol = host->protocol;
    api.game_version = host->game_version;
    api.library_name = host->library_name;
    api.user = host->user;
    api.read_client_state = host->read_client_state;
    api.read_inventory = host->read_inventory;
    api.read_touch = host->read_touch;
    api.compute_ai = host->compute_ai;
    api.tick_identity = host->tick_identity;
    api.submit_actions = host->submit_actions;
    api.register_tick_callback = host->register_tick_callback;
    api.get_image_info = host->get_image_info;
    api.install_client_hooks = host->install_client_hooks;
    api.register_hook_dispatch = host->register_hook_dispatch;
    api.get_native_abi_certificate = host->get_native_abi_certificate;
    std::string error;
    const bool ok = runtime->attach(api, &error);
    copy_error(error, error_buffer, error_buffer_size);
    return ok ? 1 : 0;
}

void bedrockai_client_integration_detach(std::uintptr_t handle) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (runtime) runtime->detach();
}

int bedrockai_client_integration_install_tick(std::uintptr_t handle, char* error_buffer, std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime) { copy_error("invalid integration handle", error_buffer, error_buffer_size); return 0; }
    std::string error;
    const bool ok = runtime->install_tick_callback(&error);
    copy_error(error, error_buffer, error_buffer_size);
    return ok ? 1 : 0;
}

int bedrockai_client_integration_tick(std::uintptr_t handle, char* error_buffer, std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime) { copy_error("invalid integration handle", error_buffer, error_buffer_size); return 0; }
    std::string error;
    const bool ok = runtime->tick(&error);
    copy_error(error, error_buffer, error_buffer_size);
    return ok ? 1 : 0;
}

std::uint64_t bedrockai_client_integration_tick_count(std::uintptr_t handle) {
    const auto* runtime = reinterpret_cast<const bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    return runtime ? runtime->tick_count() : 0;
}

int bedrockai_client_integration_install_complete_hook_plan(std::uintptr_t handle,
                                                            const char* plan_text,
                                                            char* error_buffer,
                                                            std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime || !plan_text) { copy_error("invalid complete hook-plan arguments", error_buffer, error_buffer_size); return 0; }
    std::vector<bedrockai::ClientHookSpec> specs;
    std::string error;
    if (!bedrockai::parse_client_hook_plan(plan_text, specs, &error)) {
        copy_error(error, error_buffer, error_buffer_size);
        return 0;
    }
    if (!bedrockai::validate_complete_client_hook_plan(specs, runtime->image_info().build_id_hex, &error)) {
        copy_error(error, error_buffer, error_buffer_size);
        return 0;
    }
    const bool ok = runtime->install_complete_hook_plan(specs, &error);
    copy_error(error, error_buffer, error_buffer_size);
    return ok ? 1 : 0;
}

int bedrockai_client_integration_hook_kind_installed(std::uintptr_t handle, std::uint8_t kind) {
    const auto* runtime = reinterpret_cast<const bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime || kind > static_cast<std::uint8_t>(bedrockai::ClientHookKind::SubmitActions)) return 0;
    return runtime->hook_kind_installed(static_cast<bedrockai::ClientHookKind>(kind)) ? 1 : 0;
}

int bedrockai_client_integration_install_tick_hook(std::uintptr_t handle,
                                                    const char* required_build_id_hex,
                                                    const char* signature_text,
                                                    const char* expected_text,
                                                    char* error_buffer,
                                                    std::size_t error_buffer_size) {
    auto* runtime = reinterpret_cast<bedrockai::EmbeddedBedrockClientIntegration*>(handle);
    if (!runtime || !required_build_id_hex || !signature_text || !expected_text) {
        copy_error("invalid Tick hook arguments", error_buffer, error_buffer_size);
        return 0;
    }
    bedrockai::ClientHookSpec spec;
    spec.name = "client_tick";
    spec.kind = bedrockai::ClientHookKind::Tick;
    spec.required_build_id_hex = required_build_id_hex;
    std::string error;
    if (!bedrockai::SignaturePattern::parse(signature_text, spec.signature, &error) ||
        !bedrockai::SignaturePattern::parse(expected_text, spec.expected_bytes, &error)) {
        copy_error(error, error_buffer, error_buffer_size);
        return 0;
    }
    spec.patch_offset = 0U;
    spec.patch_size_bytes = 4U;
    const bool ok = runtime->install_verified_tick_hook(spec, &error);
    copy_error(error, error_buffer, error_buffer_size);
    return ok ? 1 : 0;
}


namespace {
void write_report_to_buffer(const std::string& report, char* buffer, std::size_t buffer_size) {
    if (!buffer || buffer_size == 0U) return;
    const std::size_t count = std::min(buffer_size - 1U, report.size());
    std::memcpy(buffer, report.data(), count);
    buffer[count] = '\0';
}
}

int bedrockai_current_client_image_report(char* buffer, std::size_t buffer_size) {
    bedrockai::ClientImageInfo image;
    std::string error;
    if (!bedrockai::inspect_current_client_image(image, &error)) {
        write_report_to_buffer("error=" + error + "\n", buffer, buffer_size);
        return 0;
    }
    write_report_to_buffer(bedrockai::format_client_image_report(image), buffer, buffer_size);
    return 1;
}

}
