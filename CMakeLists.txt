cmake_minimum_required(VERSION 3.25)

project(viam-csi
        VERSION 0.0.2
        DESCRIPTION "viam module for csi cameras"
)

set(CMAKE_CXX_STANDARD 17)

option(VIAM_CSI_ENABLE_TESTS "Build viam-csi unit tests" OFF)
set(VIAM_CSI_TARGET "jetson" CACHE STRING "Which meta.json to package: jetson or pi")
set_property(CACHE VIAM_CSI_TARGET PROPERTY STRINGS jetson pi)

find_package(viam-cpp-sdk CONFIG REQUIRED)
find_package(gstreamer CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp csi_camera.cpp utils.cpp)

target_link_libraries(${PROJECT_NAME}
        gstreamer::gstreamer
        viam-cpp-sdk::viamsdk
)

# glibc < 2.34 (focal) keeps POSIX AIO symbols in librt; newer glibc merges them
# into libc. The SDK's Conan package places libviam_rust_utils.a last in link
# order but declares -lrt before it, so the linker discards rt before seeing the
# AIO references. Tell the linker to not discard unused symbols from librt.
if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")
endif()

if(VIAM_CSI_TARGET STREQUAL "pi")
    set(VIAM_CSI_META meta-pi.json)
else()
    set(VIAM_CSI_META meta.json)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(PROGRAMS first_run.sh DESTINATION .)
install(FILES ${VIAM_CSI_META} DESTINATION . RENAME meta.json)

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
endif()
