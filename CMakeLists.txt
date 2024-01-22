cmake_minimum_required (VERSION 3.5.1)
project(advanced-server-flex VERSION "${BOOST_SUPERPROJECT_VERSION}" LANGUAGES CXX)
find_package(OpenSSL)
find_package(Boost REQUIRED COMPONENTS system json)
link_libraries(${OPENSSL_LIBRARIES})

set(JWT_INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(JWT_HEADER_FILES ${JWT_INCLUDE_PATH}/jwt-cpp/jwt.h)
foreach(traits ${JWT_JSON_TRAITS_OPTIONS})
  list(APPEND JWT_HEADER_FILES ${JWT_INCLUDE_PATH}/jwt-cpp/traits/${traits}/defaults.h
       ${JWT_INCLUDE_PATH}/jwt-cpp/traits/${traits}/traits.h)
endforeach()

set(CERTIFICATE_DIR ${CMAKE_BINARY_DIR}/certs)
file(MAKE_DIRECTORY ${CERTIFICATE_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/ssl/ DESTINATION ${CMAKE_BINARY_DIR}/certs/)
add_custom_target(generate_certificates DEPENDS
    ${CERTIFICATE_DIR}/certs/
)

file(GLOB_RECURSE JSON_FILES
  ${Boost_INCLUDE_DIRS}/boost/json/*.hpp
  ${Boost_INCLUDE_DIRS}/boost/json/*.ipp)

source_group(TREE ${Boost_INCLUDE_DIRS}/boost/json PREFIX json FILES ${JSON_FILES})

get_filename_component (BOOST_ROOT ../../ ABSOLUTE)
add_definitions (-DBOOST_ALL_STATIC_LINK=1)
add_definitions (-DBOOST_ASIO_NO_DEPRECATED=1)
add_definitions (-DBOOST_ASIO_DISABLE_BOOST_ARRAY=1)
add_definitions (-DBOOST_ASIO_DISABLE_BOOST_BIND=1)
add_definitions (-DBOOST_ASIO_DISABLE_BOOST_DATE_TIME=1)
add_definitions (-DBOOST_ASIO_DISABLE_BOOST_REGEX=1)
add_definitions (-DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)

set(BOOST_INCLUDEDIR ${BOOST_ROOT})
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
find_package(Boost COMPONENTS context filesystem system thread REQUIRED)
link_libraries(Boost::context Boost::filesystem Boost::system Boost::thread)
link_directories(${BOOST_ROOT}/stage/lib)

add_definitions (-DBOOST_ASIO_SEPARATE_COMPILATION=1)
add_definitions (-DBOOST_BEAST_SEPARATE_COMPILATION=1)

source_group (lib "/")

add_library (
    lib-asio STATIC
    lib/lib_asio.cpp
)

add_library (
    lib-asio-ssl STATIC
    lib/lib_asio_ssl.cpp
)

set_property(TARGET lib-asio-ssl PROPERTY FOLDER "static-libs")

add_library (
    lib-beast STATIC
    lib/lib_beast.cpp
)

set_property(TARGET lib-beast PROPERTY FOLDER "static-libs")

target_link_libraries(lib-beast lib-asio jwt-cpp)

if (OPENSSL_FOUND)
    source_group(include/boost/beast beast)
    source_group(common common)
    source_group("/")

    add_executable (advanced-server-flex
        ${JSON_FILES}
        ${BOOST_BEAST_FILES}
        ${PROJECT_SOURCE_DIR}/common/server_certificate.hpp
        Jamfile
        listener.hpp
        json.hpp
        shared_state.cpp
        advanced-server-flex.cpp
    )

    add_library(jwt-cpp INTERFACE)
    add_library(jwt-cpp::jwt-cpp ALIAS jwt-cpp) # To match export
    target_compile_features(jwt-cpp INTERFACE cxx_std_11)
    target_include_directories(jwt-cpp INTERFACE $<BUILD_INTERFACE:${JWT_INCLUDE_PATH}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries (advanced-server-flex
        Boost::json
        jwt-cpp
        OpenSSL::SSL OpenSSL::Crypto
        lib-asio
        lib-asio-ssl
        lib-beast
        )

endif()
