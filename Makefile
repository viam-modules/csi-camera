# CMake
BUILD_DIR := ./build
INSTALL_DIR := $(BUILD_DIR)/AppDir
BIN_DIR := ./bin

# Docker
HUB_USER := seanavery
TEST_NAME := viam-csi-test
DOCK_TAG := 0.0.1 # tag for mod/test images
BASE_TAG := 0.0.3
L4T_TAG := 35.4.1

# Package
PACK_NAME := viam-csi
PACK_TAG := latest

# CLI
TARGET ?= pi # [jetson,pi]
ifeq ($(TARGET), jetson)
	TEST_BASE=nvcr.io/nvidia/l4t-base:$(L4T_TAG)
	BASE_NAME=viam-cpp-base-jetson
	BASE_CONFIG=./etc/Dockerfile.base.l4t
	MOD_NAME=viam-csi-module-jetson
	MOD_CONFIG=./etc/Dockerfile.mod
	RECIPE=./viam-csi-jetson-arm64.yml
else ifeq ($(TARGET), pi)
	TEST_BASE=debian:bookworm
	BASE_NAME=viam-cpp-base-pi
	BASE_CONFIG=./etc/Dockerfile.base.bullseye
	MOD_NAME=viam-csi-module-pi
	MOD_CONFIG=./etc/Dockerfile.mod.pi
	RECIPE=./viam-csi-pi-arm64.yml
endif

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
	appimage-builder \
		--recipe $(RECIPE) \

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
		apt-get install -y gstreamer1.0-tools && \
		apt-get install -y libgstreamer1.0-dev \
			libgstreamer-plugins-base1.0-dev \
			libgstreamer-plugins-good1.0-dev \
			libgstreamer-plugins-bad1.0-dev; \
	elif [ "$(TARGET)" = "pi" ]; then \
		apt-get install -y --no-install-recommends software-properties-common && \
		apt-add-repository 'deb http://archive.raspberrypi.org/debian/ bullseye main' && \
		wget -qO - https://archive.raspberrypi.org/debian/raspberrypi.gpg.key | apt-key add - && \
		apt-get -y update && \
		apt-get -y install libcamera0 \
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
		cd ${HOME}/opt/src && \
		git clone https://github.com/raspberrypi/libcamera.git && \
		cd libcamera && \
		git checkout 563cd78e && \
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
		--build-arg L4T_TAG=$(L4T_TAG) \
		-f $(BASE_CONFIG) ./

# Builds docker image with viam-csi installed.
image-mod:
	docker build -t $(MOD_NAME):$(DOCK_TAG) \
		--build-arg BASE_IMG=ghcr.io/$(HUB_USER)/$(BASE_NAME):$(BASE_TAG) \
		-f $(MOD_CONFIG) ./

# Builds raw L4T docker image with viam-csi appimage.
image-test:
	docker build -t $(TEST_NAME)-$(TARGET):$(DOCK_TAG) \
		--no-cache \
		--build-arg TEST_BASE=$(TEST_BASE) \
		--build-arg PACK_TAG=$(PACK_TAG) \
		-f ./etc/Dockerfile.test ./

# Copies binary and appimage from container to host.
bin-mod:
	rm -rf $(BIN_DIR) || true && \
	mkdir -p $(BIN_DIR) && \
	docker stop viam-csi-bin || true && \
	docker rm viam-csi-bin || true && \
	docker run -d -it --name viam-csi-bin $(MOD_NAME):$(DOCK_TAG) && \
	docker cp viam-csi-bin:/root/opt/src/csi-camera/build/viam-csi ./$(BIN_DIR) && \
	docker cp viam-csi-bin:/root/opt/src/csi-camera/etc/viam-csi-$(PACK_TAG)-aarch64.AppImage ./$(BIN_DIR) && \
	docker stop viam-csi-bin

# SDK
.PHONY: build-sdk
build-sdk:
	cd viam-cpp-sdk && \
	mkdir -p build && \
	cd build && \
	cmake -DVIAMCPPSDK_USE_DYNAMIC_PROTOS=ON -DVIAMCPPSDK_OFFLINE_PROTO_GENERATION=ON .. -G Ninja && \
	ninja -j 2 && \
	sudo ninja install -j 2 && \
	sudo cp -r ./install/* /usr/local/

docker-sdk:
	docker build -t viam-cpp-sdk -f ./viam-cpp-sdk/etc/docker/Dockerfile.ubuntu.focal ./ && \
	docker run -it viam-cpp-sdk /bin/bash

# Tests
# Tests out package in a fresh container.
test-package:
	docker run \
		-e PACK_FILE=$(PACK_NAME)-$(PACK_TAG)-aarch64.AppImage \
		--device /dev/fuse \
		--cap-add SYS_ADMIN \
		$(TEST_NAME)-$(TARGET):$(DOCK_TAG)

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
