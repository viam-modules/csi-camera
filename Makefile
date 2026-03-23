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

# Conan
export CONAN_HOME := $(CURDIR)/.conan-home/.conan2
VENV_DIR := ./venv
CONAN_OUT := ./build-conan
CONAN_TEST_OUT := ./build-conan-test
CONAN_BIN := $(CONAN_OUT)/build/Release/viam-csi
CONAN_FLAGS := -s:a build_type=Release -s:a compiler.cppstd=17
CONAN_TEST_OPT := -o "&:with_tests=True"

.PHONY: conan-setup conan-install conan-build conan-build-with-tests conan-test build-conan-binary

conan-setup:
	python3 -m venv $(VENV_DIR) 2>/dev/null || pip3 install conan --ignore-installed
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; pip install conan 2>/dev/null || true
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan profile detect --force
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan remote add viamconan https://viam.jfrog.io/artifactory/api/conan/viamconan --index 0 --force || true

conan-install:
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan --version >/dev/null 2>&1 || $(MAKE) conan-setup
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan install . --output-folder=$(CONAN_OUT) --build=missing $(CONAN_FLAGS)

conan-build:
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan build . --output-folder=$(CONAN_OUT) --build=none $(CONAN_FLAGS)

conan-build-with-tests:
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan install . --output-folder=$(CONAN_TEST_OUT) --build=missing $(CONAN_FLAGS) $(CONAN_TEST_OPT)
	test -f $(VENV_DIR)/bin/activate && . $(VENV_DIR)/bin/activate; \
	conan build . --output-folder=$(CONAN_TEST_OUT) --build=none $(CONAN_FLAGS) $(CONAN_TEST_OPT)

conan-test: conan-install conan-build conan-build-with-tests
	cd $(CONAN_TEST_OUT)/build/Release && \
		. ./generators/conanrun.sh && \
		ctest --output-on-failure

build-conan-binary:
	$(MAKE) conan-install TARGET=$(TARGET)
	$(MAKE) conan-build TARGET=$(TARGET)

# Module
# Builds/installs module.
.PHONY: build
build:
	$(MAKE) build-conan-binary TARGET=$(TARGET)

# Creates appimage package.
package:
	$(MAKE) build-conan-binary TARGET=$(TARGET)
	@mkdir -p $(BUILD_DIR)
	@cp $(CONAN_BIN) $(BUILD_DIR)/viam-csi
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
