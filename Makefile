# CMake
BUILD_DIR := ./build
INSTALL_DIR := $(BUILD_DIR)/AppDir
BIN_DIR := ./bin

# Docker
HUB_USER := viam-modules/csi-camera
TEST_NAME := viam-csi-test
BASE_TAG := 0.0.6

# Package
PACK_NAME := viam-csi
PACK_TAG := latest

# CLI
TARGET ?= pi # [jetson,pi]
ifeq ($(TARGET), jetson)
	BASE_NAME=viam-cpp-base-jetson
	BASE_CONFIG=./etc/Dockerfile.base
	RECIPE=./viam-csi-jetson-arm64.yml
else ifeq ($(TARGET), pi)
	BASE_NAME=viam-cpp-base-pi
	BASE_CONFIG=./etc/Dockerfile.base.bullseye
	RECIPE=./viam-csi-pi-arm64.yml
endif

# Conan (Phase 1 opt-in path)
VENV_DIR := ./.venv
CONAN_USER_HOME := $(CURDIR)/.conan-home
CONAN_USER_BIN := $(CONAN_USER_HOME)/.local/bin/conan
CONAN_HOME := $(CONAN_USER_HOME)/.conan2
CONAN_OUT := ./build-conan
CONAN_TEST_OUT := ./build-conan-test
CONAN_FLAGS := -s:a os=Linux -s:a arch=armv8 -s:a build_type=Release -s:a compiler=gcc -s:a compiler.libcxx=libstdc++11 -s:a compiler.cppstd=17 -s:a compiler.version=$$(g++ -dumpversion | cut -d. -f1)
CONAN_TEST_OPT := -o "&:with_tests=True"

.PHONY: conan-setup conan-install conan-build conan-test build-jetson-conan

conan-setup:
	@python3 -m venv $(VENV_DIR) 2>/dev/null || true
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && \
		pip install conan; \
	else \
		mkdir -p $(CONAN_USER_HOME) && \
		HOME=$(CONAN_USER_HOME) python3 -m pip install --user conan; \
	fi
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan profile detect --force; \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) profile detect --force; \
	fi
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan remote add viamconan https://viam.jfrog.io/artifactory/api/conan/viamconan --index 0 --force; \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) remote add viamconan https://viam.jfrog.io/artifactory/api/conan/viamconan --index 0 --force; \
	fi || true

conan-install:
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan --version >/dev/null 2>&1; \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) --version >/dev/null 2>&1; \
	fi || $(MAKE) conan-setup
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan install . --output-folder=$(CONAN_OUT) --build=missing $(CONAN_FLAGS); \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) install . --output-folder=$(CONAN_OUT) --build=missing $(CONAN_FLAGS); \
	fi

conan-build:
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan --version >/dev/null 2>&1; \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) --version >/dev/null 2>&1; \
	fi || $(MAKE) conan-setup
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan build . --output-folder=$(CONAN_OUT) --build=none $(CONAN_FLAGS); \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) build . --output-folder=$(CONAN_OUT) --build=none $(CONAN_FLAGS); \
	fi

conan-test: conan-install conan-build
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan install . --output-folder=$(CONAN_TEST_OUT) --build=missing $(CONAN_FLAGS) $(CONAN_TEST_OPT); \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) install . --output-folder=$(CONAN_TEST_OUT) --build=missing $(CONAN_FLAGS) $(CONAN_TEST_OPT); \
	fi
	@if [ -x $(VENV_DIR)/bin/activate ]; then \
		. $(VENV_DIR)/bin/activate && CONAN_HOME=$(CONAN_HOME) python3 -m conan build . --output-folder=$(CONAN_TEST_OUT) --build=none $(CONAN_FLAGS) $(CONAN_TEST_OPT); \
	else \
		HOME=$(CONAN_USER_HOME) CONAN_HOME=$(CONAN_HOME) $(CONAN_USER_BIN) build . --output-folder=$(CONAN_TEST_OUT) --build=none $(CONAN_FLAGS) $(CONAN_TEST_OPT); \
	fi
	cd $(CONAN_TEST_OUT)/build/Release && \
		. ./generators/conanrun.sh && \
		ctest --output-on-failure

build-jetson-conan:
	@if [ "$(TARGET)" != "jetson" ]; then \
		echo "build-jetson-conan is only valid with TARGET=jetson"; \
		exit 1; \
	fi
	$(MAKE) conan-install TARGET=jetson
	$(MAKE) conan-build TARGET=jetson

# Module
# Builds/installs module.
.PHONY: build
build:
	rm -rf $(BUILD_DIR) | true && \
	mkdir -p build && \
	cd build && \
	cmake -DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) .. -G Ninja && \
	ninja -j $(shell nproc)

# Creates appimage cmake build.
package:
	cd etc && \
	PACK_NAME=$(PACK_NAME) \
	PACK_TAG=$(PACK_TAG) \
	appimage-builder --recipe $(RECIPE)

lint:
	./etc/run-clang-format.sh

# Removes all build and bin artifacts.
clean:
	rm -rf $(BUILD_DIR) | true && \
	rm -rf $(BIN_DIR) | true && \
	rm -rf $(INSTALL_DIR) | true \
	rm -rf ./etc/appimage-build | true && \
	rm -f ./etc/viam-csi-$(PACK_TAG)-aarch64.AppImage*

# Copies binary and appimage to bin folder
bin:
	rm -rf $(BIN_DIR) | true && \
	mkdir -p $(BIN_DIR) && \
	cp $(BUILD_DIR)/viam-csi $(BIN_DIR) && \
	cp ./etc/viam-csi-$(PACK_TAG)-aarch64.AppImage $(BIN_DIR)

dep:
	export DEBIAN_FRONTEND=noninteractive && \
	export TZ=America/New_York && \
	apt-get update && \
	if [ "$(TARGET)" = "jetson" ]; then \
		apt-get -y install libgtest-dev && \
		apt-get install -y gstreamer1.0-tools gstreamer1.0-plugins-good && \
		apt-get install -y libgstreamer1.0-dev \
			libgstreamer-plugins-base1.0-dev \
			libgstreamer-plugins-good1.0-dev \
			libgstreamer-plugins-bad1.0-dev; \
	elif [ "$(TARGET)" = "pi" ]; then \
		apt-get install -y --no-install-recommends software-properties-common && \
		apt-get -y install \
			libgstreamer1.0-dev \
			libgstreamer1.0-0 \
			gstreamer1.0-x \
			gstreamer1.0-tools \
			gstreamer1.0-plugins-base \
			gstreamer1.0-plugins-good \
			gstreamer1.0-plugins-bad \
			gstreamer1.0-plugins-ugly \
			libgstreamer-plugins-base1.0-dev && \
		apt-get -y install libgtest-dev && \
		cd /usr/src/gtest && \
		cmake ./ && \
		make && \
		apt-get install libgmock-dev && \
		cd /usr/src/googletest/googlemock/ && \
		cmake ./ && \
		make && \
		mkdir -p ${HOME}/opt/src && \
		apt-get -y install meson && \
		apt-get -y install libyaml-dev python3-yaml python3-ply python3-jinja2 && \
		pip3 install --upgrade meson && \
		cd ${HOME}/opt/src && \
		git clone https://github.com/raspberrypi/libcamera.git && \
		cd libcamera && \
		git checkout v0.5.0+rpt20250429 && \
		meson setup build --prefix=/usr && \
		ninja -C build install && \
		rm -rf ${HOME}/opt/src/libcamera; \
	else \
		echo "Unknown TARGET: $(TARGET)"; \
		echo "Must be one of: jetson, pi" \
		exit 1; \
	fi
	
# Docker
# Builds docker image with viam-cpp-sdk and helpers.
image-base:
	docker build -t $(BASE_NAME):$(BASE_TAG) \
		--memory=16g \
		-f $(BASE_CONFIG) ./

# Utils
# Installs waveshare camera overrides on Jetson.
waveshare:
	mkdir -p gen && \
	wget https://www.waveshare.com/w/upload/e/eb/Camera_overrides.tar.gz -O gen/Camera_overrides.tar.gz && \
	tar -xvf gen/Camera_overrides.tar.gz -C gen && \
	sudo cp gen/camera_overrides.isp /var/nvidia/nvcam/settings/ && \
	sudo chmod 664 /var/nvidia/nvcam/settings/camera_overrides.isp && \
	sudo chown root:root /var/nvidia/nvcam/settings/camera_overrides.isp

# Installs Arducam IMX477 driver on Jetson.
arducam:
	mkdir -p gen && \
	cd gen && \
	wget https://github.com/ArduCAM/MIPI_Camera/releases/download/v0.0.3/install_full.sh && \
	chmod +x install_full.sh && \
	./install_full.sh -m imx477

# Restarts argus service on Jetson. Run this if argus is broken.
restart-argus:
	sudo systemctl stop nvargus-daemon && \
	sudo systemctl start nvargus-daemon && \
	sudo systemctl status nvargus-daemon

# Admin
# pushes appimage to storage bucket.
push-package:
	gsutil cp $(BIN_DIR)/viam-csi-$(PACK_TAG)-aarch64.AppImage gs://packages.viam.com/apps/csi-camera/

# Pushes base docker image to github packages.
# Requires docker login to ghcr.io
push-base:
	docker tag $(BASE_NAME):$(BASE_TAG) ghcr.io/$(HUB_USER)/$(BASE_NAME):$(BASE_TAG) && \
	docker push ghcr.io/$(HUB_USER)/$(BASE_NAME):$(BASE_TAG)
