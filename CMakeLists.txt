cmake_minimum_required (VERSION 3.5.1)
project(advanced-server-flex VERSION "${BOOST_SUPERPROJECT_VERSION}" LANGUAGES CXX)
find_package(OpenSSL)
link_libraries(${OPENSSL_LIBRARIES})

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

target_link_libraries(lib-beast lib-asio)

if (OPENSSL_FOUND)
    source_group(include/boost/beast beast)
    source_group(common common)
    source_group("/")

    add_executable (advanced-server-flex
        ${BOOST_BEAST_FILES}
        ${PROJECT_SOURCE_DIR}/common/server_certificate.hpp
        Jamfile
        listener.hpp
        advanced-server-flex.cpp
    )

    set_property(TARGET advanced-server-flex PROPERTY FOLDER "example-advanced-server")
    
    target_link_libraries (advanced-server-flex
        OpenSSL::SSL OpenSSL::Crypto
        lib-asio
        lib-asio-ssl
        lib-beast
        )

endif()
