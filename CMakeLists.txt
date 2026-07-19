# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)

project(Salamis VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(salamis
    src/SalmisCluster.cpp
    src/SalmisWebConsole.cpp
    src/SalmisManagement.cpp
    src/SalmisCore.cpp
    src/SalmisRuntime.cpp
)

target_include_directories(salamis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(salamis PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(salamis PRIVATE /W4)
else()
    target_compile_options(salamis PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(salamis_cli
    src/main.cpp
)

target_link_libraries(salamis_cli PRIVATE salamis)

install(TARGETS salamis salamis_cli
    EXPORT salamisTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/salamis
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/salamis/examples
)

enable_testing()

add_executable(salamis_tests
    tests/salmis_core_tests.cpp
)

target_link_libraries(salamis_tests PRIVATE salamis)

add_test(NAME salamis_tests COMMAND salamis_tests)

set(CPACK_PACKAGE_NAME "salamis")
set(CPACK_PACKAGE_VENDOR "Salamis")
set(CPACK_PACKAGE_CONTACT "Salamis maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Distributed in-memory vector database with persistent storage")
set(CPACK_PACKAGE_DESCRIPTION
    "Salamis is a C++17 vector database with local persistence, distributed sharding, management CLI, and user/admin web consoles."
)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://example.invalid/salamis")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES TRUE)

set(CPACK_DEBIAN_PACKAGE_SECTION "database")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Salamis maintainers")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Databases")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()

include(CPack)
