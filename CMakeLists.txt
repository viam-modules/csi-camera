cmake_minimum_required(VERSION 3.25)

project(viam-csi
        VERSION 0.0.2
        DESCRIPTION "viam module for csi cameras"
)

set(CMAKE_CXX_STANDARD 17)

set(GNU_LIB_PATH /usr/lib/aarch64-linux-gnu)
set(GNU_INCLUDE_PATH /usr/include/aarch64-linux-gnu)
set(LOCAL_LIB_PATH /usr/local/lib)
set(LOCAL_INCLUDE_PATH /usr/local/include)

find_package(PkgConfig REQUIRED)
find_package(viam-cpp-sdk CONFIG REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-base-1.0)
option(VIAM_CSI_ENABLE_TESTS "Build viam-csi unit tests" ON)

include_directories(
        ${GLIB_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GNU_INCLUDE_PATH}
        ${LOCAL_INCLUDE_PATH}
)

link_directories(
        ${GLIB_LIBRARY_DIRS}
        ${GSTREAMER_LIBRARY_DIRS}
        ${GNU_LIB_PATH}
        ${LOCAL_LIB_PATH}
)

# Main executable
add_executable(${PROJECT_NAME} main.cpp csi_camera.cpp utils.cpp)

target_link_libraries(${PROJECT_NAME} 
        ${GSTREAMER_LIBRARIES} 
        viam-cpp-sdk::viamsdk
)

install(TARGETS ${PROJECT_NAME} DESTINATION usr/local/bin)


if(VIAM_CSI_ENABLE_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    set(VIAM_CSI_GTEST_LIBS)
    if(TARGET GTest::gtest AND TARGET GTest::gtest_main)
        list(APPEND VIAM_CSI_GTEST_LIBS GTest::gtest GTest::gtest_main)
    elseif(TARGET GTest::GTest AND TARGET GTest::Main)
        list(APPEND VIAM_CSI_GTEST_LIBS GTest::GTest GTest::Main)
    else()
        message(FATAL_ERROR "Unable to resolve GTest targets from find_package(GTest)")
    endif()

    # Test executable
    add_executable(${PROJECT_NAME}_test tests/unit/test_csi_camera.cpp)

    target_link_libraries(${PROJECT_NAME}_test
            ${GSTREAMER_LIBRARIES}
            ${VIAM_CSI_GTEST_LIBS}
            viam-cpp-sdk::viamsdk
    )

    # Add the test as a target
    add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)
endif()
