set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)
project(VulkanApp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Sources.cmake)
include(cmake/Targets.cmake)

# On coupe les warnings pour le fichier source C++ de VMA
set_source_files_properties(ext/vma/vma_impl.cpp PROPERTIES COMPILE_FLAGS "-w")
