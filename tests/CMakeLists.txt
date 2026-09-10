find_package(GTest REQUIRED)

set(TEST_BINARY run_tests)

add_executable(${TEST_BINARY}
  cache_test.cpp
  repository_test.cpp
  integration_test.cpp
)

target_link_libraries(${TEST_BINARY}
  PRIVATE blogcore
  PRIVATE blogservice
  PRIVATE GTest::gtest_main
  PRIVATE gRPC::grpc++
  PRIVATE protobuf::libprotobuf
)
target_include_directories(${TEST_BINARY} PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

include(GoogleTest)
gtest_discover_tests(${TEST_BINARY})
