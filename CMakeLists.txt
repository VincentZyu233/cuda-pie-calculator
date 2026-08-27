cmake_minimum_required(VERSION 3.24)

project(cuda-pie-calculator LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

# Build portable binaries for current NVIDIA desktop generations. Consumers need
# an NVIDIA driver, but do not need a separately installed CUDA runtime.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89)
endif()

find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)

add_executable(cuda-pie-calculator
    src/main.cpp
    src/pi_engine.cu
    src/resources.cpp
    src/tui.cpp
)

target_include_directories(cuda-pie-calculator PRIVATE src)
target_link_libraries(cuda-pie-calculator PRIVATE CUDA::cudart_static Threads::Threads ${CMAKE_DL_LIBS})

target_compile_definitions(cuda-pie-calculator PRIVATE $<$<CONFIG:Debug>:PIE_DEBUG_BUILD=1>)

if(MSVC)
    target_compile_options(cuda-pie-calculator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(cuda-pie-calculator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()

set_target_properties(cuda-pie-calculator PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS cuda-pie-calculator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
