#顶层Cmake，设置全局链接库以及添加子目录，和项目的头文件.h位置
#设置最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(webserver)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

#设置全局的c++标准
set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_STANDARD_REQUIRED True)

# Optional data backends.  The framework core intentionally has no data-store
# dependency; ARServer selects these targets explicitly.
set(HTTP_FRAMEWORK_WITH_MYSQL AUTO CACHE STRING "MySQL backend: AUTO, ON, or OFF")
set(HTTP_FRAMEWORK_WITH_REDIS AUTO CACHE STRING "Redis backend: AUTO, ON, or OFF")
set_property(CACHE HTTP_FRAMEWORK_WITH_MYSQL PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE HTTP_FRAMEWORK_WITH_REDIS PROPERTY STRINGS AUTO ON OFF)
if(NOT HTTP_FRAMEWORK_WITH_MYSQL MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "HTTP_FRAMEWORK_WITH_MYSQL must be AUTO, ON, or OFF")
endif()
if(NOT HTTP_FRAMEWORK_WITH_REDIS MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "HTTP_FRAMEWORK_WITH_REDIS must be AUTO, ON, or OFF")
endif()
if(NOT HTTP_FRAMEWORK_WITH_MYSQL STREQUAL "OFF")
    find_library(MYSQL_LIB mysqlclient PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
endif()
if(NOT HTTP_FRAMEWORK_WITH_REDIS STREQUAL "OFF")
    find_library(HIREDIS_LIB hiredis PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
endif()
# AuthService is exercised by the AR handler tests even when Redis is disabled.
find_library(CRYPTO_LIB crypto PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
if(HTTP_FRAMEWORK_WITH_MYSQL STREQUAL "ON" AND NOT MYSQL_LIB)
    message(FATAL_ERROR "HTTP_FRAMEWORK_WITH_MYSQL=ON but mysqlclient was not found")
endif()
if(HTTP_FRAMEWORK_WITH_REDIS STREQUAL "ON" AND NOT HIREDIS_LIB)
    message(FATAL_ERROR "HTTP_FRAMEWORK_WITH_REDIS=ON but hiredis was not found")
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#设置全局链接库
set(LIBS pthread)
if(CRYPTO_LIB)
    list(APPEND LIBS ${CRYPTO_LIB})
else()
    message(FATAL_ERROR "OpenSSL libcrypto is required for password hashing")
endif()

# MySQL / Redis 可选依赖
if(MYSQL_LIB)
    list(APPEND LIBS ${MYSQL_LIB})
    add_definitions(-DHAS_MYSQL)
    message(STATUS "MySQL found: ${MYSQL_LIB}")
else()
    message(STATUS "MySQL NOT found — building without DB support")
endif()

if(HIREDIS_LIB)
    list(APPEND LIBS ${HIREDIS_LIB})
    add_definitions(-DHAS_REDIS)
    message(STATUS "hiredis found: ${HIREDIS_LIB}")
else()
    message(STATUS "hiredis NOT found — building without Redis support")
endif()

option(ENABLE_REDIS_INTEGRATION_TESTS "Enable Redis session integration tests" OFF)
option(BUILD_TESTS "Build project test targets when tests/ is available" ON)

#添加子目录使用
add_subdirectory(memory)
add_subdirectory(src)

set(BUILD_PROJECT_TESTS OFF)
if(BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/TestSupport.h")
    enable_testing()
    set(BUILD_PROJECT_TESTS ON)
endif()

if(BUILD_PROJECT_TESTS)
add_executable(http_protocol_test tests/unit/http_protocol_test.cpp)
target_include_directories(http_protocol_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(http_protocol_test src_lib memory_lib ${LIBS})
add_test(NAME http_protocol_test COMMAND http_protocol_test)

add_executable(router_test tests/unit/router_test.cpp)
target_include_directories(router_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(router_test src_lib memory_lib ${LIBS})
add_test(NAME router_test COMMAND router_test)

add_executable(middleware_test tests/unit/middleware_test.cpp)
target_include_directories(middleware_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(middleware_test src_lib memory_lib ${LIBS})
add_test(NAME middleware_test COMMAND middleware_test)

add_executable(http_dispatcher_test tests/unit/http_dispatcher_test.cpp)
target_include_directories(http_dispatcher_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(http_dispatcher_test src_lib memory_lib ${LIBS})
add_test(NAME http_dispatcher_test COMMAND http_dispatcher_test)

add_executable(http_server_test tests/unit/http_server_test.cpp)
target_include_directories(http_server_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(http_server_test src_lib memory_lib ${LIBS})
add_test(NAME http_server_test COMMAND http_server_test)

add_executable(session_manager_test tests/unit/session_manager_test.cpp)
target_include_directories(session_manager_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(session_manager_test src_lib memory_lib ${LIBS})
add_test(NAME session_manager_test COMMAND session_manager_test)

add_executable(async_responder_test tests/unit/async_responder_test.cpp)
target_include_directories(async_responder_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(async_responder_test src_lib memory_lib ${LIBS})
add_test(NAME async_responder_test COMMAND async_responder_test)

add_executable(task_worker_pool_test tests/unit/task_worker_pool_test.cpp)
target_include_directories(task_worker_pool_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(task_worker_pool_test src_lib memory_lib ${LIBS})
add_test(NAME task_worker_pool_test COMMAND task_worker_pool_test)

add_executable(logger_level_test tests/unit/logger_level_test.cpp)
target_include_directories(logger_level_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(logger_level_test src_lib memory_lib ${LIBS})
add_test(NAME logger_level_test COMMAND logger_level_test)

add_executable(session_codec_test tests/unit/session_codec_test.cpp)
target_include_directories(session_codec_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(session_codec_test src_lib memory_lib ${LIBS})
add_test(NAME session_codec_test COMMAND session_codec_test)

add_executable(static_file_handler_test tests/unit/static_file_handler_test.cpp)
target_include_directories(static_file_handler_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(static_file_handler_test src_lib memory_lib ${LIBS})
add_test(NAME static_file_handler_test COMMAND static_file_handler_test)

add_executable(ar_handlers_test tests/unit/ar_handlers_test.cpp)
target_include_directories(ar_handlers_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_sources(ar_handlers_test PRIVATE WebApps/ARServer/src/utils/JsonUtil.cpp
                                        WebApps/ARServer/src/utils/ApiError.cpp
                                        WebApps/ARServer/src/catalog/SceneCatalog.cpp
                                        WebApps/ARServer/src/services/ArSessionValidator.cpp
                                        WebApps/ARServer/src/handlers/AuthHandler.cpp
                                        WebApps/ARServer/src/handlers/SceneHandlers.cpp
                                        WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp
                                        WebApps/ARServer/src/handlers/SessionHandlers.cpp
                                        WebApps/ARServer/src/services/AuthService.cpp
                                        WebApps/ARServer/src/services/SessionService.cpp
                                        WebApps/ARServer/src/services/SceneInteractionService.cpp
                                        WebApps/ARServer/src/services/PresenceService.cpp
                                        src/db/SceneInteractionDAO.cpp
                                        )
target_link_libraries(ar_handlers_test src_lib memory_lib ${LIBS})
if(TARGET ar_data)
    target_link_libraries(ar_handlers_test ar_data)
endif()
add_test(NAME ar_handlers_test COMMAND ar_handlers_test)

add_executable(scene_catalog_test tests/unit/scene_catalog_test.cpp
    WebApps/ARServer/src/catalog/SceneCatalog.cpp)
target_include_directories(scene_catalog_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                     ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_link_libraries(scene_catalog_test src_lib memory_lib ${LIBS})
add_test(NAME scene_catalog_test COMMAND scene_catalog_test)

add_executable(exhibition_catalog_test tests/unit/exhibition_catalog_test.cpp
    WebApps/ARServer/src/catalog/ExhibitionCatalog.cpp)
target_include_directories(exhibition_catalog_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(exhibition_catalog_test src_lib memory_lib ${LIBS})
add_test(NAME exhibition_catalog_test COMMAND exhibition_catalog_test)
set_tests_properties(exhibition_catalog_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(cached_session_store_test tests/unit/cached_session_store_test.cpp
    WebApps/ARServer/src/services/CachedSessionStore.cpp)
target_include_directories(cached_session_store_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_link_libraries(cached_session_store_test src_lib memory_lib ${LIBS})
if(TARGET ar_data)
    target_link_libraries(cached_session_store_test ar_data)
endif()
add_test(NAME cached_session_store_test COMMAND cached_session_store_test)

add_executable(presence_service_test tests/unit/presence_service_test.cpp)
target_include_directories(presence_service_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_link_libraries(presence_service_test src_lib memory_lib ${LIBS})
target_sources(presence_service_test PRIVATE WebApps/ARServer/src/services/PresenceService.cpp)
if(TARGET http_framework_redis)
    target_link_libraries(presence_service_test http_framework_redis)
endif()
add_test(NAME presence_service_test COMMAND presence_service_test)

add_executable(presence_handlers_test tests/unit/presence_handlers_test.cpp)
target_include_directories(presence_handlers_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_sources(presence_handlers_test PRIVATE
    WebApps/ARServer/src/handlers/PresenceHandlers.cpp
    WebApps/ARServer/src/services/PresenceService.cpp
    WebApps/ARServer/src/services/SessionService.cpp
    WebApps/ARServer/src/utils/ApiError.cpp
    WebApps/ARServer/src/utils/JsonUtil.cpp)
target_link_libraries(presence_handlers_test src_lib memory_lib ${LIBS})
if(TARGET http_framework_redis)
    target_link_libraries(presence_handlers_test http_framework_redis)
endif()
add_test(NAME presence_handlers_test COMMAND presence_handlers_test)

if(HIREDIS_LIB)
    add_executable(presence_redis_integration_test tests/integration/presence_redis_integration.cpp
                   WebApps/ARServer/src/services/PresenceService.cpp)
    target_include_directories(presence_redis_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
    target_link_libraries(presence_redis_integration_test http_framework_redis memory_lib ${LIBS})
    add_test(NAME presence_redis_integration
             COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration/presence_redis_integration.sh
                     $<TARGET_FILE:presence_redis_integration_test>)
endif()

add_executable(app_config_test tests/unit/app_config_test.cpp)
target_include_directories(app_config_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_sources(app_config_test PRIVATE WebApps/ARServer/src/config/AppConfig.cpp)
target_link_libraries(app_config_test src_lib memory_lib ${LIBS})
add_test(NAME app_config_test COMMAND app_config_test)

add_executable(request_middleware_test tests/unit/request_middleware_test.cpp)
target_include_directories(request_middleware_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_sources(request_middleware_test PRIVATE WebApps/ARServer/src/middleware/AuthMiddleware.cpp)
target_link_libraries(request_middleware_test src_lib memory_lib ${LIBS})
add_test(NAME request_middleware_test COMMAND request_middleware_test)
endif()

if(MYSQL_LIB)
add_executable(ar_server
    WebApps/ARServer/src/main.cpp
    WebApps/ARServer/src/ARServer.cpp
    WebApps/ARServer/src/config/AppConfig.cpp
    WebApps/ARServer/src/catalog/SceneCatalog.cpp
    WebApps/ARServer/src/utils/JsonUtil.cpp
    WebApps/ARServer/src/utils/ApiError.cpp
    WebApps/ARServer/src/handlers/AuthHandler.cpp
    WebApps/ARServer/src/handlers/PresenceHandlers.cpp
    WebApps/ARServer/src/handlers/SceneHandlers.cpp
    WebApps/ARServer/src/handlers/SceneInteractionHandlers.cpp
    WebApps/ARServer/src/handlers/SessionHandlers.cpp
    WebApps/ARServer/src/middleware/AuthMiddleware.cpp
    WebApps/ARServer/src/services/AuthService.cpp
    WebApps/ARServer/src/services/ArSessionValidator.cpp
    WebApps/ARServer/src/services/CachedSessionStore.cpp
    WebApps/ARServer/src/services/PresenceService.cpp
    WebApps/ARServer/src/services/DaoAuthStore.cpp
    WebApps/ARServer/src/services/SessionService.cpp
    WebApps/ARServer/src/services/SceneInteractionService.cpp
    WebApps/ARServer/src/services/DaoSessionStore.cpp)
target_include_directories(ar_server PRIVATE ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_link_libraries(ar_server src_lib memory_lib ${LIBS})
if(TARGET ar_data)
    target_link_libraries(ar_server ar_data)
endif()
endif()

install(TARGETS http_framework memory_lib
        EXPORT http_frameworkTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET http_framework_redis)
    install(TARGETS http_framework_redis EXPORT http_frameworkTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TARGET ar_data)
    install(TARGETS ar_data EXPORT http_frameworkTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT http_frameworkTargets
        FILE http_frameworkTargets.cmake
        NAMESPACE http_framework::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/http_framework)
configure_package_config_file(cmake/http_frameworkConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/http_frameworkConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/http_framework)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/http_frameworkConfigVersion.cmake
        VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/http_frameworkConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/http_frameworkConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/http_framework)

if(BUILD_PROJECT_TESTS AND ENABLE_REDIS_INTEGRATION_TESTS AND HIREDIS_LIB)
    add_executable(redis_session_integration_test tests/integration/redis_session_integration.cpp)
    target_link_libraries(redis_session_integration_test src_lib http_framework_redis memory_lib ${LIBS})
    add_test(NAME redis_session_integration
             COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration/redis_session_integration.sh
                     $<TARGET_FILE:redis_session_integration_test>)
endif()

if(BUILD_PROJECT_TESTS AND MYSQL_LIB)
add_executable(session_dao_test tests/unit/session_dao_test.cpp)
    target_include_directories(session_dao_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(session_dao_test src_lib ar_data memory_lib ${LIBS})
add_test(NAME session_dao_test COMMAND session_dao_test)

add_executable(scene_interaction_dao_test tests/unit/scene_interaction_dao_test.cpp
    src/db/SceneInteractionDAO.cpp)
target_include_directories(scene_interaction_dao_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(scene_interaction_dao_test src_lib ar_data memory_lib ${LIBS})
add_test(NAME scene_interaction_dao_test COMMAND scene_interaction_dao_test)

add_executable(scene_interaction_service_test tests/unit/scene_interaction_service_test.cpp)
target_include_directories(scene_interaction_service_test PRIVATE ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/WebApps/ARServer/include)
target_sources(scene_interaction_service_test PRIVATE
    WebApps/ARServer/src/catalog/SceneCatalog.cpp
    WebApps/ARServer/src/services/SessionService.cpp
    WebApps/ARServer/src/services/SceneInteractionService.cpp
    src/db/SceneInteractionDAO.cpp)
target_link_libraries(scene_interaction_service_test src_lib memory_lib ${LIBS})
if(TARGET ar_data)
    target_link_libraries(scene_interaction_service_test ar_data)
endif()
add_test(NAME scene_interaction_service_test COMMAND scene_interaction_service_test)
endif()

if(BUILD_PROJECT_TESTS AND MYSQL_LIB AND HIREDIS_LIB)
    add_executable(session_cache_test tests/session_cache_test.cpp)
    target_link_libraries(session_cache_test src_lib ar_data memory_lib ${LIBS})
    add_test(NAME session_cache_test COMMAND session_cache_test)
endif()
