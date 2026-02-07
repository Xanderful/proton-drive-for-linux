.PHONY: help setup dev build build-web build-native build-debug build-linux build-appimage build-deb build-rpm clean test fmt lint setup-sync sync-status sync-start sync-stop install-debian uninstall

help:
	@echo "Proton Drive Linux - Available commands:"
	@echo ""
	@echo "Setup & Development:"
	@echo "  make setup         - Install dependencies and initialize project"
	@echo "  make dev           - Build and run for development"
	@echo "  make install-debian - Full installation on Debian/Ubuntu"
	@echo ""
	@echo "Building:"
	@echo "  make build         - Build all (WebClients + native binary)"
	@echo "  make build-web     - Build web frontend only"
	@echo "  make build-native  - Build native binary only"
	@echo "  make build-debug   - Build with debug symbols"
	@echo ""
	@echo "Packaging:"
	@echo "  make build-appimage - Build portable AppImage"
	@echo "  make build-deb     - Build DEB package"
	@echo "  make build-rpm     - Build RPM package"
	@echo ""
	@echo "Folder Sync (rclone):"
	@echo "  make setup-sync    - Setup rclone and enable folder sync to ~/ProtonDrive"
	@echo "  make sync-status   - Check sync mount status"
	@echo "  make sync-start    - Start the sync service"
	@echo "  make sync-stop     - Stop the sync service"
	@echo ""
	@echo "Maintenance:"
	@echo "  make fmt           - Format code (C++)"
	@echo "  make lint          - Lint code (C++)"
	@echo "  make test          - Run tests"
	@echo "  make clean         - Clean build artifacts"
	@echo "  make uninstall     - Uninstall Proton Drive"

setup:
	bash scripts/setup.sh

dev: build-native
	./src-native/build/proton-drive --debug

build: build-web build-native

build-web:
	npm run build:web

build-native:
	mkdir -p src-native/build
	cd src-native/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make

build-debug:
	mkdir -p src-native/build
	cd src-native/build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make

build-linux: build
	# Package creation logic would go here, utilizing CPack or similar

build-appimage:
	bash scripts/build-local-appimage.sh

build-deb:
	bash scripts/build-local-deb.sh

build-rpm:
	bash scripts/build-local-rpm.sh

install-debian:
	bash scripts/install-debian-ubuntu.sh --full

fmt:
	# C++ formatting command (e.g., clang-format)
	find src-native/src -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i 2>/dev/null || echo "clang-format not installed"

lint:
	# C++ linting command (e.g., clang-tidy)
	clang-tidy src-native/src/*.cpp -- -I src-native/src 2>/dev/null || echo "clang-tidy not installed"

test:
	cd WebClients/applications/drive && npm test

clean:
	rm -rf src-native/build
	rm -rf WebClients/applications/drive/dist
	rm -rf WebClients/applications/drive/build
	rm -rf dist
	rm -rf node_modules
	rm -rf tools

# Folder sync targets (rclone-based)
setup-sync:
	bash scripts/setup-rclone.sh
	bash scripts/setup-sync-service.sh

sync-status:
	bash scripts/setup-sync-service.sh --status

sync-start:
	bash scripts/setup-sync-service.sh --start

sync-stop:
	bash scripts/setup-sync-service.sh --stop

uninstall:
	bash scripts/uninstall.sh
