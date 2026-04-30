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
#   make tidy-fix     # apply clang-tidy --fix to staged C++ files
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
        install uninstall clean format lint tidy tidy-fix lint-all lint-fix \
        precommit wiki udev doctor docs docs-check

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
	@# Auto-detect Wayland session when neither WAYLAND_DISPLAY nor DISPLAY is set
	@# in the invoking shell (common when running from a TTY, SSH, or a tool that
	@# doesn't inherit the niri/sway/hyprland session env). Without this, Qt 6.5+
	@# falls back to xcb, fails to load it, and aborts with a misleading message
	@# about xcb-cursor0. Picks the first wayland-* socket in $$XDG_RUNTIME_DIR.
	@if [ -z "$$WAYLAND_DISPLAY" ] && [ -z "$$DISPLAY" ]; then \
	    sock=$$(ls "$${XDG_RUNTIME_DIR:-/run/user/$$(id -u)}"/wayland-* 2>/dev/null | grep -v '\.lock$$' | head -1); \
	    if [ -n "$$sock" ]; then \
	        echo "→ no display env, auto-detected Wayland socket: $$sock"; \
	        export WAYLAND_DISPLAY="$$(basename $$sock)"; \
	        export QT_QPA_PLATFORM=wayland; \
	    fi; \
	fi; \
	exec $(BIN)

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

tidy: configure ## Run clang-tidy on staged/changed files only
	@git diff --cached --name-only --diff-filter=AM | grep -E '\.(cpp|hpp|cc|h)$$' | \
	    xargs -r bash scripts/run-clang-tidy.sh

tidy-fix: configure ## Run clang-tidy with --fix on staged/changed files (applies safe fixes)
	@git diff --cached --name-only --diff-filter=AM | grep -E '\.(cpp|hpp|cc|h)$$' | \
	    xargs -r clang-tidy -p $(BUILD_DIR_DEBUG) --fix --fix-errors --quiet
	@echo "Auto-fixable clang-tidy issues applied. Re-stage and review:  git add -p"

lint-all: ## Run EVERY linter via pre-commit (clang-format, ruff, yamllint, shellcheck, …)
	@command -v pre-commit >/dev/null || pip install --user pre-commit
	@pre-commit run --all-files

lint-fix: ## Run every linter with auto-fix enabled
	@command -v pre-commit >/dev/null || pip install --user pre-commit
	@pre-commit run --all-files --show-diff-on-failure || true
	@echo "Auto-fixable issues applied. Review & commit."

precommit: ## Install the git pre-commit, commit-msg and pre-push hooks
	@command -v pre-commit >/dev/null || pip install --user pre-commit
	@pre-commit install
	@pre-commit install --hook-type commit-msg
	@pre-commit install --hook-type pre-push
	@echo "pre-commit hooks installed (commit + commit-msg + pre-push)."

wiki: ## Preview GitHub Wiki locally on http://localhost:3030
	@command -v mdbook >/dev/null || { echo "Install mdbook: cargo install mdbook"; exit 1; }
	@cd docs/wiki && mdbook serve --port 3030

docs: ## Regenerate README + wiki AUTOGEN blocks from docs/_data/devices.yaml
	@python3 -c 'import yaml' 2>/dev/null || pip install --user --quiet pyyaml
	@python3 scripts/generate-docs.py

docs-check: ## Fail if README or wiki AUTOGEN blocks are out of date (CI mode)
	@python3 -c 'import yaml' 2>/dev/null || pip install --user --quiet pyyaml
	@python3 scripts/generate-docs.py --check

udev: ## (Linux) (re)install the udev rule without a full install
	@sudo install -m 644 resources/linux/99-ajazz.rules /etc/udev/rules.d/
	@sudo udevadm control --reload-rules
	@sudo udevadm trigger
	@echo "udev rule installed."

doctor: ## Diagnose your environment: toolchain, Qt, Python, devices
	@bash scripts/doctor.sh

watch-ci: ## Watch GitHub Actions runs for HEAD until they all conclude
	@bash scripts/watch-ci.sh
