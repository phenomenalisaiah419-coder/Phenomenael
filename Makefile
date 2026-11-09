# Portable build for the Bedrock AI Controller (no CMake required).
# Same strict warning policy as CMakeLists.txt.  Usage:
#   make -j4 all        # core library + every test binary
#   make check          # build and run every test binary
#   make check T=v36    # build and run a single test (tests/test_v36.cpp)
#   make clean
#
# OpenSSL (optional, enables the real crypto path):
#   make OPENSSL_INC=/usr/include/node OPENSSL_LIB=/usr/lib/x86_64-linux-gnu/libcrypto.so.3

CXX      ?= g++
OPT      ?= -O1
BUILD    ?= build_make
STD      := -std=c++17
WARN     := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
INC      := -Icore/include -Iandroid
CXXFLAGS := $(STD) $(OPT) $(WARN) $(INC) -MMD -MP $(EXTRA_CXXFLAGS)
LDLIBS   := -lz -lpthread

ifneq ($(OPENSSL_LIB),)
CXXFLAGS += -DBEDROCKAI_HAVE_OPENSSL=1
ifneq ($(OPENSSL_INC),)
CXXFLAGS += -I$(OPENSSL_INC)
endif
LDLIBS   += $(OPENSSL_LIB)
endif

ARCH := $(shell uname -m)
CORE_SRC := $(sort $(wildcard core/src/*.cpp core/src/wire/*.cpp))
ifneq ($(filter aarch64 arm64,$(ARCH)),)
CORE_SRC := $(filter-out core/src/arm64_tick_stub_fallback.cpp,$(CORE_SRC))
CORE_ASM := core/src/arm64_tick_stub.S
endif
CORE_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRC))
ifneq ($(CORE_ASM),)
CORE_OBJ += $(patsubst %.S,$(BUILD)/%.o,$(CORE_ASM))
endif
CORE_LIB := $(BUILD)/libcore.a

TEST_SRC := $(sort $(wildcard tests/test_*.cpp))
TEST_BIN := $(patsubst tests/test_%.cpp,$(BUILD)/bin/%_tests,$(TEST_SRC))
TEST_BIN := $(patsubst $(BUILD)/bin/core_tests,$(BUILD)/bin/core_tests,$(TEST_BIN))

# Tests that need the Android-side glue / the local loopback server.
EXTRA_v14 := tests/interoperability/local_bedrock_server.cpp android/native_controller.cpp
EXTRA_v15 := android/nethernet_host.cpp android/native_controller.cpp
EXTRA_companion_brain_adapter := android/companion_brain_adapter.cpp

.PHONY: all check clean core
all: $(CORE_LIB) $(TEST_BIN)
core: $(CORE_LIB)

$(CORE_LIB): $(CORE_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CXX) -c $< -o $@

.SECONDEXPANSION:
$(BUILD)/bin/%_tests: tests/test_$$*.cpp $(CORE_LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(EXTRA_$*) $(CORE_LIB) $(LDLIBS) -o $@

ifdef T
check: $(BUILD)/bin/$(T)_tests
	$(BUILD)/bin/$(T)_tests
else
check: $(TEST_BIN)
	@fail=0; for t in $(TEST_BIN); do \
	  if $$t >$(BUILD)/$$(basename $$t).log 2>&1; then echo "PASS $$(basename $$t)"; \
	  else echo "FAIL $$(basename $$t)"; fail=1; fi; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"
endif

clean:
	rm -rf $(BUILD)

-include $(CORE_OBJ:.o=.d)

# Regenerate the skill catalogue document from the registry:  make catalog > docs/SKILL_CATALOG.md
.PHONY: catalog
catalog: $(CORE_LIB)
	@$(CXX) $(CXXFLAGS) tools/skill_catalog_dump.cpp $(CORE_LIB) $(LDLIBS) -o $(BUILD)/skill_catalog_dump
	@$(BUILD)/skill_catalog_dump
