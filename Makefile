# ============================================================================
# AJAZZ Control Center — friendly Makefile
# ============================================================================
# The real build system is CMake; this Makefile is a set of shortcuts so you
# don't need to remember the CMake preset names.
#
# Most-used targets:
#   make              # alias for `make build`
#   make bootstrap    # install every system dep, then build  (first-time)
#   make build        # configure + incremental build (debug)
#   make release      # configure + build (release, no sanitizers)
#   make run          # build + launch the app
#   make test         # build + run unit tests
#   make package      # produce deb/rpm (linux), dmg (macos), msi (windows)
#   make install      # install into /usr/local   (Linux / macOS)
#   make uninstall    # remove what `make install` put down
#   make clean        # delete build directory
#   make format       # run clang-format on every C++ file
#   make lint         # run clang-tidy on every C++ file
#   make wiki         # preview the wiki locally (requires `mdbook`)
#   make help         # show this list
# ============================================================================

BUILD_DIR_DEBUG   := build/dev
BUILD_DIR_RELEASE := build/release
BIN               := $(BUILD_DIR_DEBUG)/src/app/ajazz-control-center
BIN_RELEASE       := $(BUILD_DIR_RELEASE)/src/app/ajazz-control-center

# Use `nproc` on Linux, `sysctl -n hw.ncpu` on macOS
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: default help bootstrap build release configure run test package \
        install uninstall clean format lint wiki udev doctor

default: build

help:
	@awk 'BEGIN{FS=":.*##"} /^[a-zA-Z_-]+:.*##/ {printf "  \033[1m%-14s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo ""
	@echo "First-time setup: \033[1mmake bootstrap\033[0m"

bootstrap: ## Install every system dependency, then do a first build
	@bash scripts/bootstrap-dev.sh

configure: ## Configure CMake with preset 'dev'
	@cmake --preset dev

build: configure ## Debug build (fast, with sanitizers)
	@cmake --build --preset dev --parallel $(JOBS)

release: ## Release build (optimized)
	@cmake --preset release
	@cmake --build --preset release --parallel $(JOBS)

run: build ## Build and launch the app
	@$(BIN)

test: build ## Run all unit and integration tests
	@ctest --preset dev --output-on-failure

package: ## Build distribution packages for this platform
	@cmake --preset release
	@cmake --build --preset release --parallel $(JOBS) --target package
	@echo ""
	@echo "Packages written to $(BUILD_DIR_RELEASE)/:"
	@ls -1 $(BUILD_DIR_RELEASE)/*.{deb,rpm,dmg,msi,tar.gz} 2>/dev/null || true

install: release ## Install into /usr/local (Linux/macOS)
	@cmake --install $(BUILD_DIR_RELEASE)
	@if [ "$$(uname)" = "Linux" ]; then \
	    sudo install -m 644 resources/linux/99-ajazz.rules /etc/udev/rules.d/ ; \
	    sudo udevadm control --reload-rules ; \
	    sudo udevadm trigger ; \
	    echo "udev rule installed — no logout required." ; \
	fi

uninstall: ## Remove files placed by `make install`
	@if [ -f $(BUILD_DIR_RELEASE)/install_manifest.txt ]; then \
	    xargs rm -fv < $(BUILD_DIR_RELEASE)/install_manifest.txt ; \
	fi
	@sudo rm -f /etc/udev/rules.d/99-ajazz.rules 2>/dev/null || true

clean: ## Delete every build directory
	@rm -rf build

format: ## Auto-format every C++ source file (clang-format)
	@find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
	@echo "Formatted."

lint: configure ## Run clang-tidy across the tree
	@cmake --build --preset dev --target clang-tidy

wiki: ## Preview GitHub Wiki locally on http://localhost:3030
	@command -v mdbook >/dev/null || { echo "Install mdbook: cargo install mdbook"; exit 1; }
	@cd docs/wiki && mdbook serve --port 3030

udev: ## (Linux) (re)install the udev rule without a full install
	@sudo install -m 644 resources/linux/99-ajazz.rules /etc/udev/rules.d/
	@sudo udevadm control --reload-rules
	@sudo udevadm trigger
	@echo "udev rule installed."

doctor: ## Diagnose your environment: toolchain, Qt, Python, devices
	@bash scripts/doctor.sh
