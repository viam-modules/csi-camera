cmake_minimum_required(VERSION 3.25)

project(viam-csi
        VERSION 0.0.2
        DESCRIPTION "viam module for csi cameras"
)

set(CMAKE_CXX_STANDARD 17)

option(VIAM_CSI_ENABLE_TESTS "Build viam-csi unit tests" OFF)

find_package(viam-cpp-sdk CONFIG REQUIRED)
find_package(gstreamer CONFIG REQUIRED)

# Native libcamera discovery backend (Pi). Off by default so the standard build
# and CI need no libcamera; enable on a Pi build once it's hardware-validated.
option(VIAM_CSI_WITH_LIBCAMERA "Build the libcamera discovery backend (Pi)" OFF)

set(VIAM_CSI_SOURCES main.cpp csi_camera.cpp utils.cpp discovery.cpp)
if(VIAM_CSI_WITH_LIBCAMERA)
    list(APPEND VIAM_CSI_SOURCES discovery_libcamera.cpp)
endif()

add_executable(${PROJECT_NAME} ${VIAM_CSI_SOURCES})

target_link_libraries(${PROJECT_NAME}
        gstreamer::gstreamer
        viam-cpp-sdk::viamsdk
)

if(VIAM_CSI_WITH_LIBCAMERA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
    target_link_libraries(${PROJECT_NAME} PkgConfig::LIBCAMERA)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VIAM_CSI_WITH_LIBCAMERA)
endif()

# glibc < 2.34 (focal) keeps POSIX AIO symbols in librt; newer glibc merges them
# into libc. The SDK's Conan package places libviam_rust_utils.a last in link
# order but declares -lrt before it, so the linker discards rt before seeing the
# AIO references. Tell the linker to not discard unused symbols from librt.
if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(PROGRAMS first_run.sh DESTINATION .)
install(FILES meta.json DESTINATION .)

# `cmake --build <dir> --target package` assembles module.tar.gz from the
# install rules above.
set(CPACK_PACKAGE_NAME "viam-csi")
set(CPACK_PACKAGE_FILE_NAME "module")
set(CPACK_GENERATOR "TGZ")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
include(CPack)

if(VIAM_CSI_ENABLE_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(${PROJECT_NAME}_test tests/unit/test_csi_camera.cpp)

    target_link_libraries(${PROJECT_NAME}_test
            gstreamer::gstreamer
            GTest::gtest
            GTest::gtest_main
            viam-cpp-sdk::viamsdk
    )

    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)

    # Separate exe: tests include the .cpp under test, so each lives in its own
    # binary to avoid duplicate symbols across translation units.
    add_executable(${PROJECT_NAME}_discovery_test tests/unit/test_discovery.cpp)

    target_link_libraries(${PROJECT_NAME}_discovery_test
            gstreamer::gstreamer
            GTest::gtest
            GTest::gtest_main
            viam-cpp-sdk::viamsdk
    )

    add_test(NAME ${PROJECT_NAME}_discovery_test COMMAND ${PROJECT_NAME}_discovery_test)
endif()
