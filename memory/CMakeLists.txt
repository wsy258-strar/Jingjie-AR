#获取当前目录下的所有源文件项目
file(GLOB MEMORY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/*cc)

#创建静态库或共享库
add_library(memory_lib SHARED ${MEMORY_FILE})
target_include_directories(memory_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
