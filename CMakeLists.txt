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

# The CUDA calculation engine is exposed through a C ABI. The Rust ratatui front
# end loads this shared library dynamically at runtime.
add_library(cuda_pie_engine SHARED
    src/engine_c_api.cpp
    src/pi_engine.cu
    src/resources.cpp
)

target_include_directories(cuda_pie_engine PRIVATE src)
target_link_libraries(cuda_pie_engine PRIVATE CUDA::cudart_static Threads::Threads ${CMAKE_DL_LIBS})

target_compile_definitions(cuda_pie_engine PRIVATE PIE_EXPORTS $<$<CONFIG:Debug>:PIE_DEBUG_BUILD=1>)

if(MSVC)
    target_compile_options(cuda_pie_engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(cuda_pie_engine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()

set_target_properties(cuda_pie_engine PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(GNUInstallDirs)
install(TARGETS cuda_pie_engine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
