# Host (macOS) build of the engine: a static library, the headless test host
# and the small test tools. The iOS/Mac app compiles the same sources through
# app/project.yml.
#
#   make            build/host/orehost and build/host/libore.a
#   make tests      build the test tools
#   make clean

BUILD   := ../build/host
CXX     := clang++
CC      := clang
CXXFLAGS:= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Isrc -Ithird_party
CFLAGS  := -O2 -g -w -Ithird_party
# SQLite, zlib and AudioToolbox are for the mining kit (docs/MINING.md).
FRAMEWORKS := -framework CoreText -framework CoreGraphics -framework CoreFoundation -framework Foundation \
              -framework AudioToolbox -lsqlite3 -lz

CPP_SRCS := $(shell find src -name '*.cpp')
MM_SRCS  := $(shell find src -name '*.mm')
C_SRCS   := $(shell find src -name '*.c')
OBJS := $(CPP_SRCS:%.cpp=$(BUILD)/obj/%.o) $(MM_SRCS:%.mm=$(BUILD)/obj/%.o) $(C_SRCS:%.c=$(BUILD)/obj/%.o)
DEPS := $(OBJS:.o=.d)

all: $(BUILD)/orehost

$(BUILD)/libore.a: $(OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: %.mm
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fobjc-arc -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/host/stb_write.o: tests/stb_write_impl.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -Ithird_party -c $< -o $@

$(BUILD)/orehost: host/main.mm $(BUILD)/libore.a $(BUILD)/obj/host/stb_write.o
	$(CXX) $(CXXFLAGS) -fobjc-arc host/main.mm $(BUILD)/obj/host/stb_write.o $(BUILD)/libore.a $(FRAMEWORKS) -o $@

tests: $(BUILD)/elg_all $(BUILD)/vm_smoke $(BUILD)/movie_test $(BUILD)/movie_flow $(BUILD)/save_roundtrip $(BUILD)/quick_save $(BUILD)/command_input $(BUILD)/image_blend $(BUILD)/native_coverage $(BUILD)/mining_test $(BUILD)/menu_fade $(BUILD)/wait_cursor $(BUILD)/dict_test $(BUILD)/kit_test $(BUILD)/anki_app_test $(BUILD)/anki_target_test $(BUILD)/dict_probe

$(BUILD)/elg_all: tests/elg_all.cpp $(BUILD)/libore.a $(BUILD)/obj/host/stb_write.o
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/vm_smoke: tests/vm_smoke.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/movie_test: tests/movie_test.cpp $(BUILD)/libore.a $(BUILD)/obj/host/stb_write.o
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/movie_flow: tests/movie_flow.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/save_roundtrip: tests/save_roundtrip.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/quick_save: tests/quick_save.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/command_input: tests/command_input.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/image_blend: tests/image_blend.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/native_coverage: tests/native_coverage.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/mining_test: tests/mining_test.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/menu_fade: tests/menu_fade.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/wait_cursor: tests/wait_cursor.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

# The mining kit (docs/MINING.md); both take the engine folder: build/host/kit_test engine
$(BUILD)/dict_test: tests/dict_test.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) -Itests $^ $(FRAMEWORKS) -o $@

$(BUILD)/kit_test: tests/kit_test.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) -Itests $^ $(FRAMEWORKS) -o $@

# Tries a dictionary of one's own against the importer: build/host/dict_probe (its file says how it is used)
$(BUILD)/dict_probe: tests/dict_probe.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

# The Anki app on the same device (docs/MINING.md); the second takes the engine folder too.
$(BUILD)/anki_app_test: tests/anki_app_test.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

$(BUILD)/anki_target_test: tests/anki_target_test.cpp $(BUILD)/libore.a
	$(CXX) $(CXXFLAGS) $^ $(FRAMEWORKS) -o $@

clean:
	rm -rf $(BUILD)/obj $(BUILD)/libore.a $(BUILD)/orehost

-include $(DEPS)

.PHONY: all tests clean
