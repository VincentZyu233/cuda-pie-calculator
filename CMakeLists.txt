cmake_minimum_required(VERSION 3.24)

project(cuda-pie-calculator LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

# Emit Maxwell-compatible code and PTX so current NVIDIA GPUs can JIT a native
# image. Consumers still need an NVIDIA driver, but not a CUDA runtime install.
set(CMAKE_CUDA_ARCHITECTURES "52" CACHE STRING "CUDA GPU architectures")

find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
    GIT_TAG v7.0.3
)
FetchContent_MakeAvailable(ftxui)

add_executable(cuda-pie-calculator
    src/main.cpp
    src/pi_engine.cu
    src/resources.cpp
    src/tui.cpp
)

target_include_directories(cuda-pie-calculator PRIVATE src ${ftxui_SOURCE_DIR}/include)
# Use LINK_ONLY so FTXUI PUBLIC /utf-8 (set by its CMakeLists) does not leak
# into nvcc. We re-add /utf-8 only for C++ below.
target_link_libraries(cuda-pie-calculator PRIVATE CUDA::cudart_static Threads::Threads ${CMAKE_DL_LIBS} $<LINK_ONLY:ftxui::ftxui>)

target_compile_definitions(cuda-pie-calculator PRIVATE $<$<CONFIG:Debug>:PIE_DEBUG_BUILD=1>)

if(MSVC)
    target_compile_definitions(cuda-pie-calculator PRIVATE NOMINMAX)
    target_compile_options(cuda-pie-calculator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
else()
    target_compile_options(cuda-pie-calculator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()

set_target_properties(cuda-pie-calculator PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS cuda-pie-calculator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
