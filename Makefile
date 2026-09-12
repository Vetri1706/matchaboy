CXX := clang++
CLANG_FORMAT ?= clang-format
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Werror -Wpedantic
CORE := src/cpu.cpp src/mmu.cpp src/timer.cpp src/mbc.cpp src/ppu.cpp src/oam.cpp src/apu.cpp src/snapshot.cpp
HEADERS := $(wildcard include/dmg/*.hpp)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIBEXT := dylib
SHARED := -dynamiclib
else
LIBEXT := so
SHARED := -shared
endif

.PHONY: all test platform platform-test platform-sanitize platform-tsan verify verify-all sanitize format clean
all: build/dmg build/unit_tests build/oam_tests build/ppu_edge_tests build/apu_tests build/snapshot_tests build/libmatcha.$(LIBEXT)
all: build/gym_benchmark
all: build/gym_tests
build:
	mkdir -p build
build/dmg: $(CORE) src/harness.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) src/harness.cpp -o $@
build/unit_tests: $(CORE) tests/unit_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/unit_tests.cpp -o $@
build/oam_tests: $(CORE) tests/oam_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/oam_tests.cpp -o $@
build/ppu_edge_tests: $(CORE) tests/ppu_edge_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/ppu_edge_tests.cpp -o $@
build/apu_tests: src/apu.cpp tests/apu_tests.cpp include/dmg/apu.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/apu.cpp tests/apu_tests.cpp -o $@
build/snapshot_tests: $(CORE) tests/snapshot_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/snapshot_tests.cpp -o $@
build/libmatcha.$(LIBEXT): $(CORE) src/gym.cpp $(HEADERS) include/dmg/gym.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread -fPIC $(SHARED) $(CORE) src/gym.cpp -o $@
build/gym_benchmark: $(CORE) src/gym.cpp src/gym_benchmark.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread $(CORE) src/gym.cpp src/gym_benchmark.cpp -o $@
build/gym_benchmark_lto: $(CORE) src/gym.cpp src/gym_benchmark.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -flto -pthread $(CORE) src/gym.cpp src/gym_benchmark.cpp -o $@
build/gym_tests: $(CORE) src/gym.cpp tests/gym_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread $(CORE) src/gym.cpp tests/gym_tests.cpp -o $@
build/netplay_tests: src/netplay.cpp tests/netplay_tests.cpp include/dmg/netplay.hpp include/dmg/socket_platform.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread src/netplay.cpp tests/netplay_tests.cpp -o $@
build/netplay: $(CORE) src/netplay.cpp src/netplay_main.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread $(CORE) src/netplay.cpp src/netplay_main.cpp -o $@
ifeq ($(UNAME_S),Darwin)
CMAKE ?= $(if $(wildcard .venv/bin/cmake),$(CURDIR)/.venv/bin/cmake,cmake)
.PHONY: mac-gui build/autopsy
# The native player now includes bundled GBA, arcade resources and Audio Queue.
# CMake owns that dependency graph; keep the legacy build/autopsy entry point
# as a launcher for its complete app. A bare executable symlink prevents
# NSBundle from finding the arcade resources when invoked directly.
mac-gui: | build
	"$(CMAKE)" -S . -B build/mac-gui -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	"$(CMAKE)" --build build/mac-gui --target autopsy --parallel 4
	"$(CMAKE)" -E rm -rf build/MatchaAutopsy.app
	"$(CMAKE)" -E copy_directory build/mac-gui/MatchaAutopsy.app build/MatchaAutopsy.app
	"$(CMAKE)" -E rm -f build/autopsy
	printf '#!/bin/sh\nexec "$$(dirname "$$0")/MatchaAutopsy.app/Contents/MacOS/MatchaAutopsy" "$$@"\n' > build/autopsy
	chmod +x build/autopsy
build/autopsy: mac-gui
platform: mac-gui
endif
build/autopsy_tests: $(CORE) src/autopsy.cpp tests/autopsy_tests.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DENABLE_AUTOPSY -pthread $(CORE) src/autopsy.cpp tests/autopsy_tests.cpp -o $@
platform: all build/netplay build/netplay_tests build/autopsy_tests
test: build/unit_tests build/oam_tests build/ppu_edge_tests build/apu_tests build/snapshot_tests build/gym_tests
	./build/unit_tests
	./build/oam_tests
	./build/ppu_edge_tests
	./build/apu_tests
	./build/snapshot_tests
	./build/gym_tests
platform-test: platform
	$(MAKE) test
	./build/netplay_tests
	./build/autopsy_tests
platform-tsan: | build
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -pthread -fsanitize=thread $(CORE) src/gym.cpp tests/gym_tests.cpp -o build/gym_tests_tsan
	./build/gym_tests_tsan
platform-sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -pthread -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) src/gym.cpp tests/gym_tests.cpp -o build/gym_tests_sanitize
	./build/gym_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/snapshot_tests.cpp -o build/snapshot_tests_sanitize
	./build/snapshot_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer src/apu.cpp tests/apu_tests.cpp -o build/apu_tests_sanitize
	./build/apu_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -pthread -fsanitize=address,undefined -fno-omit-frame-pointer src/netplay.cpp tests/netplay_tests.cpp -o build/netplay_tests_sanitize
	./build/netplay_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -DENABLE_AUTOPSY -pthread -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) src/autopsy.cpp tests/autopsy_tests.cpp -o build/autopsy_tests_sanitize
	./build/autopsy_tests_sanitize
verify: all
	python3 tools/verify.py
verify-all:
	python3 tools/verify_all.py
sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/unit_tests.cpp -o build/unit_tests_sanitize
	./build/unit_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/oam_tests.cpp -o build/oam_tests_sanitize
	./build/oam_tests_sanitize
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/ppu_edge_tests.cpp -o build/ppu_edge_tests_sanitize
	./build/ppu_edge_tests_sanitize
format:
	$(CLANG_FORMAT) -i $(CORE) src/harness.cpp tests/*.cpp $(HEADERS)
clean:
	$(RM) build/dmg build/unit_tests build/oam_tests build/ppu_edge_tests build/*_sanitize
