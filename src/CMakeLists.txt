file(GLOB FRAMEWORK_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/timer/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/Session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/SessionCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/SessionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/SessionStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/router/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/cors/*.cpp)

# BUILD_SHARED_LIBS selects the public artifact type; neither database backend
# nor hiredis is linked by this target.
add_library(http_framework ${FRAMEWORK_SRC})
add_library(src_lib ALIAS http_framework)
target_include_directories(http_framework
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/base>
           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/log>
           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/net>
           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/timer>
           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/http>
           $<INSTALL_INTERFACE:include>
           $<INSTALL_INTERFACE:include/base>
           $<INSTALL_INTERFACE:include/log>
           $<INSTALL_INTERFACE:include/net>
           $<INSTALL_INTERFACE:include/timer>
           $<INSTALL_INTERFACE:include/http>)
target_link_libraries(http_framework PUBLIC memory_lib pthread)

if(HIREDIS_LIB)
    add_library(http_framework_redis
        ${CMAKE_CURRENT_SOURCE_DIR}/cache/RedisConnectionPool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/session/RedisSessionStorage.cpp)
    target_link_libraries(http_framework_redis PUBLIC http_framework ${HIREDIS_LIB})
    target_include_directories(http_framework_redis PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
endif()

if(MYSQL_LIB)
    set(AR_DATA_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/db/DBWorkerPool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/db/MySQLConnectionPool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/db/SessionDAO.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/db/SceneInteractionDAO.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/db/ArtworkInteractionDAO.cpp)
    if(HIREDIS_LIB)
        list(APPEND AR_DATA_SRC
            ${CMAKE_CURRENT_SOURCE_DIR}/cache/SessionCache.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/cache/TwoLevelCache.cpp)
    endif()
    add_library(ar_data ${AR_DATA_SRC})
    target_include_directories(ar_data PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
    target_link_libraries(ar_data PUBLIC http_framework ${MYSQL_LIB})
    if(TARGET http_framework_redis)
        target_link_libraries(ar_data PUBLIC http_framework_redis)
    endif()
endif()
