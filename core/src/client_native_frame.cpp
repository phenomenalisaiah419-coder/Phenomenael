#include "core/client_native_frame.hpp"

#include <cmath>
#include <utility>

namespace bedrockai {
namespace {

void set_error_value(std::string* error, std::string value) {
    if (error) *error = std::move(value);
}

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_rot2(const Rot2& value) {
    return std::isfinite(value.pitch) && std::isfinite(value.yaw);
}

} // namespace

void ClientNativeFrameAssembler::set_error(std::string* error, const char* message) {
    set_error_value(error, message ? message : "native frame assembler error");
}

bool ClientNativeFrameAssembler::finite(double value) {
    return std::isfinite(value);
}

bool ClientNativeFrameAssembler::validate_state(const ClientStateView& state, std::string* error) {
    if (!finite(state.player.health) || state.player.health < 0.0) {
        set_error(error, "native ReadState supplied invalid player health");
        return false;
    }
    if (!finite_vec3(state.player.position) || !finite_vec3(state.player.velocity) ||
        !finite_rot2(state.player.rotation) || !finite(state.player.head_yaw)) {
        set_error(error, "native ReadState supplied non-finite player state");
        return false;
    }
    if (!finite_vec3(state.last_correction.authoritative_position) ||
        !finite_rot2(state.last_correction.authoritative_rotation) ||
        !finite(state.last_correction.distance_error)) {
        set_error(error, "native ReadState supplied non-finite server correction");
        return false;
    }
    if (state.entity_count > 4096U || state.block_count > 16384U) {
        set_error(error, "native ReadState collection count exceeds controller bound");
        return false;
    }
    if ((state.entity_count != 0U && state.entities == nullptr) ||
        (state.block_count != 0U && state.blocks == nullptr)) {
        set_error(error, "native ReadState contains a null collection pointer");
        return false;
    }
    for (std::size_t i = 0U; i < state.entity_count; ++i) {
        const auto& entity = state.entities[i];
        if (!finite_vec3(entity.position) || !finite_vec3(entity.velocity) || !finite(entity.health)) {
            set_error(error, "native ReadState contains a non-finite entity");
            return false;
        }
        if (entity.health < 0.0) {
            set_error(error, "native ReadState contains a negative entity health");
            return false;
        }
    }
    return true;
}

bool ClientNativeFrameAssembler::validate_inventory(const ClientInventoryView&, std::string*) {
    // InventoryState contains bounded scalar/container types owned by the existing core.
    return true;
}

bool ClientNativeFrameAssembler::validate_touch(const EmbeddedTouchInput& touch, std::string* error) {
    if (!finite(touch.move_x) || !finite(touch.move_y) ||
        !finite(touch.look_dx) || !finite(touch.look_dy)) {
        set_error(error, "native ReadTouch supplied non-finite input");
        return false;
    }
    return true;
}

bool ClientNativeFrameAssembler::validate_metadata(const ClientHookEventMetadata& metadata, std::string* error) {
    if (metadata.version != kClientHookEventAbiVersion || metadata.protocol != 2193U) {
        set_error(error, "native hook event metadata version/protocol mismatch");
        return false;
    }
    return true;
}

bool ClientNativeFrameAssembler::begin_state_locked(const ClientHookEventMetadata& metadata, const ClientStateView& state, std::string* error) {
    if (metadata.tick != state.tick) {
        set_error(error, "native ReadState metadata tick does not match state tick");
        return false;
    }
    if (has_seen_tick_ && state.tick < last_seen_tick_) {
        set_error(error, "native ReadState tick moved backwards");
        return false;
    }
    if (state.tick <= last_consumed_tick_) {
        set_error(error, "native ReadState tick is stale or already consumed");
        return false;
    }
    if (has_seen_tick_ && state.tick == last_seen_tick_ && state_ready_) {
        set_error(error, "duplicate native ReadState for one tick generation");
        return false;
    }
    if (has_seen_tick_ && state.tick > last_seen_tick_) {
        inventory_ready_ = false;
        touch_ready_ = false;
        state_ready_ = false;
        inventory_ = ClientInventoryView{};
        touch_ = EmbeddedTouchInput{};
    }
    state_ = state;
    last_seen_tick_ = state.tick;
    has_seen_tick_ = true;
    state_ready_ = true;
    return true;
}

bool ClientNativeFrameAssembler::capture_state(const ClientHookEventMetadata& metadata, const ClientStateView& state, std::string* error) {
    Guard guard(lock_);
    if (!guard.locked()) { set_error(error, "native ReadState frame lock is busy"); return false; }
    if (!validate_metadata(metadata, error) || !validate_state(state, error)) return false;
    return begin_state_locked(metadata, state, error);
}

bool ClientNativeFrameAssembler::capture_inventory(const ClientHookEventMetadata& metadata, const ClientInventoryView& inventory, std::string* error) {
    Guard guard(lock_);
    if (!guard.locked()) { set_error(error, "native ReadInventory frame lock is busy"); return false; }
    if (!validate_metadata(metadata, error)) return false;
    if (!has_seen_tick_ || !state_ready_) {
        set_error(error, "native ReadInventory arrived before ReadState");
        return false;
    }
    if (metadata.tick != last_seen_tick_) {
        set_error(error, "native ReadInventory tick does not match pending ReadState generation");
        return false;
    }
    if (inventory_ready_) {
        set_error(error, "duplicate native ReadInventory for one tick generation");
        return false;
    }
    if (!validate_inventory(inventory, error)) return false;
    inventory_ = inventory;
    inventory_ready_ = true;
    return true;
}

bool ClientNativeFrameAssembler::capture_touch(const ClientHookEventMetadata& metadata, const EmbeddedTouchInput& touch, std::string* error) {
    Guard guard(lock_);
    if (!guard.locked()) { set_error(error, "native ReadTouch frame lock is busy"); return false; }
    if (!validate_metadata(metadata, error)) return false;
    if (!has_seen_tick_ || !state_ready_) {
        set_error(error, "native ReadTouch arrived before ReadState");
        return false;
    }
    if (metadata.tick != last_seen_tick_) {
        set_error(error, "native ReadTouch tick does not match pending ReadState generation");
        return false;
    }
    if (touch_ready_) {
        set_error(error, "duplicate native ReadTouch for one tick generation");
        return false;
    }
    if (!validate_touch(touch, error)) return false;
    touch_ = touch;
    touch_ready_ = true;
    return true;
}

bool ClientNativeFrameAssembler::consume(const ClientHookEventMetadata& metadata, ClientNativeFrame& out, std::string* error) {
    Guard guard(lock_);
    if (!guard.locked()) { set_error(error, "native Tick frame lock is busy"); return false; }
    if (!validate_metadata(metadata, error)) return false;
    if (!state_ready_ || !inventory_ready_ || !touch_ready_) {
        set_error(error, "native Tick frame is incomplete");
        return false;
    }
    if (metadata.tick != last_seen_tick_) {
        set_error(error, "native Tick metadata does not match pending frame generation");
        return false;
    }
    if (last_seen_tick_ <= last_consumed_tick_) {
        set_error(error, "native Tick frame is stale or already consumed");
        return false;
    }
    out.tick = last_seen_tick_;
    out.state = state_;
    out.inventory = inventory_;
    out.touch = touch_;
    last_consumed_tick_ = last_seen_tick_;
    state_ready_ = false;
    inventory_ready_ = false;
    touch_ready_ = false;
    return true;
}

void ClientNativeFrameAssembler::reset() {
    Guard guard(lock_);
    if (!guard.locked()) return;
    last_seen_tick_ = 0U;
    last_consumed_tick_ = 0U;
    state_ = ClientStateView{};
    inventory_ = ClientInventoryView{};
    touch_ = EmbeddedTouchInput{};
    has_seen_tick_ = false;
    state_ready_ = false;
    inventory_ready_ = false;
    touch_ready_ = false;
}

} // namespace bedrockai
