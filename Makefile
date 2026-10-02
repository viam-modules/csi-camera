TARGET ?= jetson # [jetson,pi]

.PHONY: build test lint clean waveshare arducam restart-argus

# Builds module.tar.gz for TARGET.
build:
	TARGET=$(TARGET) ./bin/build.sh

# Builds and runs unit tests.
test:
	conan export etc/conan/gstreamer
	conan install . -o "&:with_tests=True" --build=missing -c tools.system.package_manager:mode=install
	cmake --preset conan-release
	cmake --build --preset conan-release
	VIAM_CSI_DEVICE=$(TARGET) VIAM_CSI_TEST_MODE=1 ctest --test-dir build/Release --output-on-failure

lint:
	./bin/lint.sh

clean:
	rm -rf build module module.tar.gz CMakeUserPresets.json

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
