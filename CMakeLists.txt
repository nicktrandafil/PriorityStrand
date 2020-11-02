cmake_minimum_required(VERSION 2.8)

project(priority_strand)
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE include)

option(${PROJECT_NAME}_ENABLE_TESTING "Enable testing" OFF)

if(${PROJECT_NAME}_ENABLE_TESTING)
    find_package(Catch2 QUIET REQUIRED)

    add_executable(test_${PROJECT_NAME}
        test/test.cpp
        test/priority_strand.cpp
    )

    target_link_libraries(test_${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2 rt pthread)
    set_target_properties(test_${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
endif()

option(${PROJECT_NAME}_ENABLE_BENCHMARKING "Enable benchmarking" OFF)

if(${PROJECT_NAME}_ENABLE_BENCHMARKING)
    find_package(benchmark QUIET REQUIRED)

    add_executable(benchmark_${PROJECT_NAME}
        benchmark/priority_strand.cpp
    )

    target_link_libraries(benchmark_${PROJECT_NAME} benchmark::benchmark ${PROJECT_NAME})
    set_target_properties(benchmark_${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
endif()
