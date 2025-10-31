package integration

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.viam.com/rdk/components/camera"
	"go.viam.com/rdk/config"
	"go.viam.com/rdk/logging"
	"go.viam.com/rdk/robot"
	robotimpl "go.viam.com/rdk/robot/impl"
	"go.viam.com/rdk/utils"
	"go.viam.com/test"
)

const (
	componentName       = "csi-cam-1"
	modulePath          = "../../etc"
	testTimeoutDuration = 5 * time.Second
	testTickDuration    = 100 * time.Millisecond
)

func setUpViamServer(ctx context.Context, configString string, loggerName string, _ *testing.T) (robot.Robot, error) {
	logger := logging.NewLogger(loggerName)

	cfg, err := config.FromReader(ctx, "default.json", bytes.NewReader([]byte(configString)), logger, nil)
	if err != nil {
		return nil, err
	}

	r, err := robotimpl.RobotFromConfig(ctx, cfg, nil, logger)
	if err != nil {
		return nil, err
	}

	return r, nil
}

func TestCameraServer(t *testing.T) {
	logger := logging.NewLogger("csi-cam-tests")
	logger.Info("Starting CSI Camera Integration Tests")

	// Get absolute path to module
	cwd, err := os.Getwd()
	test.That(t, err, test.ShouldBeNil)

	// Try to find extracted AppImage first (CI), then fall back to AppImage (local)
	etcPath := filepath.Join(cwd, modulePath)
	absModulePath := filepath.Join(etcPath, "squashfs-root", "AppRun")

	// Check if extracted version exists (CI)
	if _, err := os.Stat(absModulePath); os.IsNotExist(err) {
		// Fall back to AppImage (local development)
		files, err := filepath.Glob(filepath.Join(etcPath, "*.AppImage"))
		if err != nil || len(files) == 0 {
			t.Fatalf("Failed to find AppImage or extracted AppRun in %s: %v", etcPath, err)
		}
		absModulePath = files[0]
	}
	logger.Infof("Using module path: %s", absModulePath)

	t.Run("With a configured robot", func(t *testing.T) {
		timeoutCtx, cancel := context.WithTimeout(context.Background(), time.Minute)
		defer cancel()
		configString := fmt.Sprintf(`
			{
				"components": [
					{
					"name": "%v",
					"api": "rdk:component:camera",
					"model": "viam:camera:csi-pi",
					"attributes": {},
					"depends_on": []
					}
				],
				"modules": [
					{
					"type": "local",
					"name": "viam_csi-cam-pi",
					"executable_path": "%v"
					}
				]
			}
			`, componentName, absModulePath)
		robot, err := setUpViamServer(context.Background(), configString, "csi-cam-robot", t)
		test.That(t, err, test.ShouldBeNil)
		test.That(t, robot, test.ShouldNotBeNil)
		defer robot.Close(timeoutCtx)

		cam, err := camera.FromRobot(robot, componentName)
		test.That(t, err, test.ShouldBeNil)
		test.That(t, cam, test.ShouldNotBeNil)
		defer cam.Close(timeoutCtx)

		t.Run("GetImage", func(t *testing.T) {
			timeout := time.After(testTimeoutDuration)
			tick := time.Tick(testTickDuration)
			for {
				select {
				case <-timeout:
					t.Fatal("timed out waiting for Get image method (one image)")
				case <-tick:
					img, err := camera.DecodeImageFromCamera(timeoutCtx, utils.MimeTypeJPEG, nil, cam)
					if err != nil {
						continue
					}
					test.That(t, img, test.ShouldNotBeNil)
					return
				}
			}
		})

		t.Run("GetImages", func(t *testing.T) {
			timeout := time.After(testTimeoutDuration)
			tick := time.Tick(testTickDuration)
			for {
				select {
				case <-timeout:
					t.Fatal("timed out waiting for Get images method (multiple images)")
				case <-tick:
					images, metadata, err := cam.Images(timeoutCtx)
					if err != nil {
						continue
					}
					test.That(t, images, test.ShouldNotBeNil)
					test.That(t, len(images), test.ShouldBeGreaterThan, 0)
					test.That(t, metadata, test.ShouldNotBeNil)
					return
				}
			}
		})

		t.Run("GetProperties", func(t *testing.T) {
			props, err := cam.Properties(timeoutCtx)
			if err != nil {
				t.Fatalf("Failed to get camera properties: %v", err)
			}
			test.That(t, props, test.ShouldNotBeNil)
		})
	})
	logger.Info("Completed CSI Camera Integration Tests")
}
