.PHONY: help setup dev build build-debug build-appimage clean fmt lint setup-sync sync-status sync-start sync-stop install uninstall

help:
	@echo "Proton Drive Linux - Available commands:"
	@echo ""
	@echo "Setup & Development:"
	@echo "  make setup         - Install dependencies and initialize project"
	@echo "  make dev           - Build and run for development (debug mode)"
	@echo "  make install       - Install binary to /usr/local/bin"
	@echo ""
	@echo "Building:"
	@echo "  make build         - Build release binary"
	@echo "  make build-debug   - Build with debug symbols"
	@echo ""
	@echo "Packaging:"
	@echo "  make build-appimage - Build portable AppImage"
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
	@echo "  make clean         - Clean build artifacts"
	@echo "  make uninstall     - Uninstall Proton Drive"

setup:
	bash scripts/setup.sh

dev: build-debug
	./src-native/build/proton-drive --debug

build:
	mkdir -p src-native/build
	cd src-native/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$$(nproc)

build-debug:
	mkdir -p src-native/build
	cd src-native/build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$$(nproc)

build-appimage:
	bash scripts/build-local-appimage.sh

install:
	sudo install -Dm755 src-native/build/proton-drive /usr/local/bin/proton-drive
	sudo install -Dm644 src-native/packaging/proton-drive.desktop /usr/share/applications/proton-drive.desktop
	@echo "Installed proton-drive to /usr/local/bin/"

fmt:
	find src-native/src -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i 2>/dev/null || echo "clang-format not installed"

lint:
	clang-tidy src-native/src/*.cpp -- -I src-native/src 2>/dev/null || echo "clang-tidy not installed"

clean:
	rm -rf src-native/build

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
