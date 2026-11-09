cmake_minimum_required(VERSION 3.16)
project(bedrock_ai_controller_26_50_v0_35 LANGUAGES CXX ASM)
option(BEDROCKAI_BUILD_ANDROID_JNI "Build the Android JNI shared library" OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(core
  core/src/arbiter.cpp
  core/src/survival.cpp
  core/src/protocol_adapter.cpp
  core/src/transport.cpp
  core/src/state_reconciler.cpp
  core/src/human_intent.cpp
  core/src/mechanics_probe.cpp
  core/src/movement_controller.cpp
  core/src/aim_controller.cpp
  core/src/combat_controller.cpp
  core/src/projectile_solver.cpp
  core/src/gameplay_brain.cpp
  core/src/gameplay_control_loop.cpp
  core/src/bedrock2193_observation_decoder.cpp
  core/src/server_rules.cpp
  core/src/item_knowledge.cpp
  core/src/shared_control.cpp
  core/src/bridge_controller.cpp
  core/src/utility_controller.cpp
  core/src/survival_controller.cpp
  core/src/deployment.cpp
  core/src/wire/player_auth_input.cpp
  core/src/wire/movement_action_sender.cpp
  core/src/world_grid.cpp
  core/src/hazard_scan.cpp
  core/src/intent_predictor.cpp
  core/src/takeover_authority.cpp
  core/src/skill_learner.cpp
  core/src/opponent_model.cpp
  core/src/item_learner.cpp
  core/src/automation_policy.cpp
  core/src/technique_registry.cpp
  core/src/survival_guardian.cpp
  core/src/sim_world.cpp
  core/src/control_hub.cpp
  core/src/wire/movement_feedback_bridge.cpp
  core/src/wire/inventory_attack.cpp
  core/src/game_batch.cpp
  core/src/network_engine.cpp
  core/src/session_bootstrap.cpp
  core/src/raknet_transport.cpp
  core/src/raknet_reliability.cpp
  core/src/socket_datagram.cpp
  core/src/bedrock_crypto.cpp
  core/src/login_payload.cpp
  core/src/auth_cache.cpp
  core/src/bedrock_handshake.cpp
  core/src/secure_runtime.cpp
  core/src/recording.cpp
  core/src/network_recovery.cpp
  core/src/replay_backend.cpp
  core/src/nethernet_transport.cpp
  core/src/embedded_client_bridge.cpp
  core/src/bedrock_client_integration.cpp
  core/src/aarch64_signature.cpp
  core/src/hook_binding.cpp
  core/src/hook_plan.cpp
  core/src/client_process_runtime.cpp
  core/src/arm64_inline_hook.cpp
  core/src/client_abi_adapter.cpp
  core/src/client_hook_dispatch.cpp
  core/src/client_native_abi.cpp
  core/src/client_native_frame.cpp
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)$")
  target_sources(core PRIVATE core/src/arm64_tick_stub.S)
else()
  target_sources(core PRIVATE core/src/arm64_tick_stub_fallback.cpp)
endif()
target_include_directories(core PUBLIC core/include)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
if(ANDROID)
  find_library(BEDROCKAI_ZLIB z)
  if(BEDROCKAI_ZLIB)
    set(BEDROCKAI_HAVE_ZLIB TRUE)
  else()
    message(FATAL_ERROR "Android zlib library not found")
  endif()
else()
  find_package(ZLIB REQUIRED)
  set(BEDROCKAI_ZLIB ZLIB::ZLIB)
endif()
find_package(OpenSSL QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
  target_compile_definitions(core PUBLIC BEDROCKAI_HAVE_OPENSSL=1)
  target_link_libraries(core PUBLIC OpenSSL::Crypto)
else()
  message(WARNING "OpenSSL not found: crypto provider will be unavailable")
endif()
target_link_libraries(core PUBLIC ${BEDROCKAI_ZLIB})
if(WIN32)
  target_link_libraries(core PUBLIC ws2_32)
endif()

if(ANDROID AND BEDROCKAI_BUILD_ANDROID_JNI)
  add_library(bedrockai_jni SHARED
    android/native_controller.cpp
    android/android_jca_crypto_provider.cpp
    android/nethernet_host.cpp
    android/src/main/cpp/bedrock_ai_jni.cpp
    android/companion_brain_adapter.cpp
    android/companion_brain_jni.cpp
  )
  target_include_directories(bedrockai_jni PRIVATE android)
  target_compile_options(bedrockai_jni PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
  target_link_libraries(bedrockai_jni PRIVATE core)
  find_library(BEDROCKAI_ANDROID_LOG log)
  if(BEDROCKAI_ANDROID_LOG)
    target_link_libraries(bedrockai_jni PRIVATE ${BEDROCKAI_ANDROID_LOG})
  endif()
  set_target_properties(bedrockai_jni PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
endif()

enable_testing()

add_executable(core_tests tests/test_core.cpp)
target_link_libraries(core_tests PRIVATE core)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME core_tests COMMAND core_tests)

add_executable(v04_tests tests/test_v04.cpp)
target_link_libraries(v04_tests PRIVATE core)
target_compile_options(v04_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v04_tests COMMAND v04_tests)

add_executable(v05_tests tests/test_v05.cpp)
target_link_libraries(v05_tests PRIVATE core)
target_compile_options(v05_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v05_tests COMMAND v05_tests)

add_executable(v06_tests tests/test_v06.cpp)
target_link_libraries(v06_tests PRIVATE core)
target_compile_options(v06_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v06_tests COMMAND v06_tests)

add_executable(v07_tests tests/test_v07.cpp)
target_link_libraries(v07_tests PRIVATE core)
target_compile_options(v07_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v07_tests COMMAND v07_tests)

add_executable(v08_tests tests/test_v08.cpp)
target_link_libraries(v08_tests PRIVATE core)
target_compile_options(v08_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v08_tests COMMAND v08_tests)

add_executable(v09_tests tests/test_v09.cpp)
target_link_libraries(v09_tests PRIVATE core)
target_compile_options(v09_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v09_tests COMMAND v09_tests)

add_executable(v10_tests tests/test_v10.cpp)
target_link_libraries(v10_tests PRIVATE core)
target_compile_options(v10_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v10_tests COMMAND v10_tests)

add_executable(v11_tests tests/test_v11.cpp)
target_link_libraries(v11_tests PRIVATE core)
target_compile_options(v11_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v11_tests COMMAND v11_tests)

add_executable(v12_tests tests/test_v12.cpp)
target_link_libraries(v12_tests PRIVATE core)
target_compile_options(v12_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v12_tests COMMAND v12_tests)

add_executable(v13_tests tests/test_v13.cpp)
target_link_libraries(v13_tests PRIVATE core)
target_compile_options(v13_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v13_tests COMMAND v13_tests)

add_executable(v14_tests
  tests/test_v14.cpp
  tests/interoperability/local_bedrock_server.cpp
  android/native_controller.cpp
)
target_link_libraries(v14_tests PRIVATE core)
target_include_directories(v14_tests PRIVATE android tests/interoperability)
target_compile_options(v14_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v14_tests COMMAND v14_tests)

add_executable(v15_tests tests/test_v15.cpp android/nethernet_host.cpp android/native_controller.cpp)
target_link_libraries(v15_tests PRIVATE core)
target_include_directories(v15_tests PRIVATE android)
target_compile_options(v15_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v15_tests COMMAND v15_tests)

add_executable(live_compat_runner tests/live_compat_runner.cpp)
target_link_libraries(live_compat_runner PRIVATE core)
target_compile_options(live_compat_runner PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

add_executable(v16_tests tests/test_v16.cpp)
target_link_libraries(v16_tests PRIVATE core)
target_compile_options(v16_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v16_tests COMMAND v16_tests)

add_executable(v17_tests tests/test_v17.cpp)
target_link_libraries(v17_tests PRIVATE core)
target_compile_options(v17_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v17_tests COMMAND v17_tests)

add_executable(v18_tests tests/test_v18.cpp)
target_link_libraries(v18_tests PRIVATE core)
target_compile_options(v18_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v18_tests COMMAND v18_tests)

add_executable(v19_tests tests/test_v19.cpp)
target_link_libraries(v19_tests PRIVATE core)
target_compile_options(v19_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v19_tests COMMAND v19_tests)

add_executable(v20_tests tests/test_v20.cpp)
target_link_libraries(v20_tests PRIVATE core)
target_compile_options(v20_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v20_tests COMMAND v20_tests)

add_executable(v21_tests tests/test_v21.cpp)
target_link_libraries(v21_tests PRIVATE core)
target_compile_options(v21_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v21_tests COMMAND v21_tests)

add_executable(v22_tests tests/test_v22.cpp)
target_link_libraries(v22_tests PRIVATE core)
target_compile_options(v22_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v22_tests COMMAND v22_tests)

add_executable(v23_tests tests/test_v23.cpp)
target_link_libraries(v23_tests PRIVATE core)
target_compile_options(v23_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v23_tests COMMAND v23_tests)

add_executable(v24_tests tests/test_v24.cpp)
target_link_libraries(v24_tests PRIVATE core)
target_compile_options(v24_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v24_tests COMMAND v24_tests)

add_executable(v25_tests tests/test_v25.cpp)
target_link_libraries(v25_tests PRIVATE core)
target_compile_options(v25_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v25_tests COMMAND v25_tests)

add_executable(v26_tests tests/test_v26.cpp)
target_link_libraries(v26_tests PRIVATE core)
target_compile_options(v26_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v26_tests COMMAND v26_tests)

add_executable(v27_tests tests/test_v27.cpp)
target_link_libraries(v27_tests PRIVATE core)
target_compile_options(v27_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v27_tests COMMAND v27_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME xbox_login_protocol_tests
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xbox_login.py)
  add_test(NAME android_cache_export_tests
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_export_android_cache.py)
endif()

add_executable(v28_tests tests/test_v28.cpp)
target_link_libraries(v28_tests PRIVATE core)
target_compile_options(v28_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v28_tests COMMAND v28_tests)

add_executable(v29_tests tests/test_v29.cpp)
target_link_libraries(v29_tests PRIVATE core)
target_compile_options(v29_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v29_tests COMMAND v29_tests)

add_executable(v30_tests tests/test_v30.cpp)
target_link_libraries(v30_tests PRIVATE core)
target_compile_options(v30_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v30_tests COMMAND v30_tests)

add_executable(v31_tests tests/test_v31.cpp)
target_link_libraries(v31_tests PRIVATE core)
target_compile_options(v31_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v31_tests COMMAND v31_tests)

add_executable(v32_tests tests/test_v32.cpp)
target_link_libraries(v32_tests PRIVATE core)
target_compile_options(v32_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v32_tests COMMAND v32_tests)

add_executable(v33_tests tests/test_v33.cpp)
target_link_libraries(v33_tests PRIVATE core)
target_compile_options(v33_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v33_tests COMMAND v33_tests)

add_executable(v34_tests tests/test_v34.cpp)
target_link_libraries(v34_tests PRIVATE core)
target_compile_options(v34_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v34_tests COMMAND v34_tests)

add_executable(v35_tests tests/test_v35.cpp)
target_link_libraries(v35_tests PRIVATE core)
target_compile_options(v35_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v35_tests COMMAND v35_tests)

add_executable(v36_tests tests/test_v36.cpp)
target_link_libraries(v36_tests PRIVATE core)
target_compile_options(v36_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v36_tests COMMAND v36_tests)

add_executable(v37_tests tests/test_v37.cpp)
target_link_libraries(v37_tests PRIVATE core)
target_compile_options(v37_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v37_tests COMMAND v37_tests)

add_executable(v38_tests tests/test_v38.cpp)
target_link_libraries(v38_tests PRIVATE core)
target_compile_options(v38_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME v38_tests COMMAND v38_tests)

add_executable(companion_brain_adapter_tests tests/test_companion_brain_adapter.cpp android/companion_brain_adapter.cpp)
target_include_directories(companion_brain_adapter_tests PRIVATE android)
target_link_libraries(companion_brain_adapter_tests PRIVATE core)
target_compile_options(companion_brain_adapter_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
add_test(NAME companion_brain_adapter_tests COMMAND companion_brain_adapter_tests)
