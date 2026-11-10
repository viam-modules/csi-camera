cmake_minimum_required(VERSION 3.25)

project(viam-csi
        VERSION 0.0.2
        DESCRIPTION "viam module for csi cameras"
)

set(CMAKE_CXX_STANDARD 17)

option(VIAM_CSI_ENABLE_TESTS "Build viam-csi unit tests" OFF)

find_package(viam-cpp-sdk CONFIG REQUIRED)
find_package(gstreamer CONFIG REQUIRED)

add_library(viam-csi-cam csi_camera.cpp utils.cpp discovery.cpp)

target_include_directories(viam-csi-cam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(viam-csi-cam PUBLIC
        gstreamer::gstreamer
        viam-cpp-sdk::viamsdk
)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PUBLIC viam-csi-cam)

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

    target_link_libraries(${PROJECT_NAME}_test PUBLIC
            viam-csi-cam
            GTest::gtest
            GTest::gtest_main
    )

    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)

    add_executable(${PROJECT_NAME}_discovery_test tests/unit/test_discovery.cpp)

    target_link_libraries(${PROJECT_NAME}_discovery_test PUBLIC
            viam-csi-cam
            GTest::gtest
            GTest::gtest_main
    )

    add_test(NAME ${PROJECT_NAME}_discovery_test COMMAND ${PROJECT_NAME}_discovery_test)
endif()
