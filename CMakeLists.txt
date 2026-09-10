cmake_minimum_required(VERSION 3.16)
project(PracticalCppBackend VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

option(BUILD_TESTS "Build unit and integration tests" ON)
option(BUILD_SERVER "Build the standalone HTTP/HTTPS server" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Protobuf / gRPC code generation -------------------------------------
set(PROTO_FILES
  proto/blog_service.proto
)

set(PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}/proto/blog_service.pb.cc)
set(PROTO_HDRS ${CMAKE_CURRENT_BINARY_DIR}/proto/blog_service.pb.h)
set(GRPC_SRCS ${CMAKE_CURRENT_BINARY_DIR}/proto/blog_service.grpc.pb.cc)
set(GRPC_HDRS ${CMAKE_CURRENT_BINARY_DIR}/proto/blog_service.grpc.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/proto
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
  ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}/proto
       --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/proto
       -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       blog_service.proto
  DEPENDS ${PROTO_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/proto
  COMMENT "Generating C++ sources from proto definitions"
)

add_custom_target(proto_gen DEPENDS ${PROTO_SRCS} ${GRPC_SRCS})

# --- Core library (cache + db + models) ----------------------------------
add_library(blogcore STATIC
  src/common/observability.cpp
  src/config/app_config.cpp
  src/model/time_utils.cpp
  src/cache/lru_cache.cpp
  src/cache/lfu_cache.cpp
  src/cache/rr_cache.cpp
  src/cache/post_cache.cpp
  src/db/blog_repository.cpp
)

target_link_libraries(blogcore
  PUBLIC mongo::mongocxx_shared
)

target_include_directories(blogcore PUBLIC src)

# --- gRPC service library ------------------------------------------------
add_library(blogservice
  src/api/blog_service_impl.cpp
  ${PROTO_SRCS} ${GRPC_SRCS}
)
add_dependencies(blogservice proto_gen)

target_link_libraries(blogservice
  PUBLIC blogcore
  PUBLIC gRPC::grpc++ protobuf::libprotobuf
)

target_include_directories(blogservice PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}/proto
)

# --- gRPC server executable ----------------------------------------------
add_executable(blog_grpc_server
  src/api/grpc_server_main.cpp
)
target_link_libraries(blog_grpc_server PRIVATE blogservice)

# --- gRPC client executable ----------------------------------------------
add_executable(blog_grpc_client
  src/api/grpc_client_main.cpp
)
target_link_libraries(blog_grpc_client PRIVATE blogservice)

# --- HTTP server executable ----------------------------------------------
add_library(bloghttp
  src/server/http_app.cpp
)
target_link_libraries(bloghttp PUBLIC blogcore)
target_include_directories(bloghttp PUBLIC src/server src)

if(BUILD_SERVER)
  add_executable(blog_http_server
    src/server/http_server_main.cpp
  )
  target_link_libraries(blog_http_server PRIVATE bloghttp)
endif()

# --- Tests ----------------------------------------------------------------
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
