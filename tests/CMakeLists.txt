find_package(GTest REQUIRED)

add_executable(unit_tests
  cache_test.cpp
  config_test.cpp
  http_server_test.cpp
)

target_link_libraries(unit_tests
  PRIVATE bloghttp
  PRIVATE GTest::gtest_main
  PRIVATE Threads::Threads
)

add_executable(grpc_unit_tests
  grpc_service_test.cpp
)

target_link_libraries(grpc_unit_tests
  PRIVATE blogservice
  PRIVATE GTest::gtest_main
)

add_executable(integration_tests
  repository_test.cpp
  integration_test.cpp
)

target_link_libraries(integration_tests
  PRIVATE blogservice
  PRIVATE GTest::gtest_main
  PRIVATE gRPC::grpc++
  PRIVATE protobuf::libprotobuf
)
target_include_directories(integration_tests PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/../proto
)

include(GoogleTest)
gtest_discover_tests(unit_tests PROPERTIES LABELS "unit")
gtest_discover_tests(grpc_unit_tests PROPERTIES LABELS "unit")
gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
