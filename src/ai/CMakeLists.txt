#
# setup
#
set( AI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}  CACHE INTERNAL "" )


#
# ai server
#
if( BUILD_SERVERS )
message( STATUS "Creating target ai-server" )
file(GLOB AI_HEADERS ${AI_SOURCE_DIR}/*.hpp)
file(GLOB AI_SOURCES ${AI_SOURCE_DIR}/*.cpp)
set( DEPENDENCIES common )
set( LIBRARIES ${GLOBAL_LIBRARIES} )
set( INCLUDE_DIRS ${GLOBAL_INCLUDE_DIRS} ${COMMON_BASE_INCLUDE_DIRS} ${RA_INCLUDE_DIRS} )
set( DEFINITIONS "${GLOBAL_DEFINITIONS} ${COMMON_BASE_DEFINITIONS}" )
set( SOURCE_FILES ${COMMON_BASE_HEADERS} ${COMMON_HEADERS} ${AI_HEADERS} ${AI_SOURCES} )
source_group( common FILES ${COMMON_BASE_HEADERS} ${COMMON_HEADERS} )
source_group( ai FILES ${AI_HEADERS} ${AI_SOURCES} )
include_directories( ${INCLUDE_DIRS} )

add_executable( ai-server ${SOURCE_FILES} )
add_dependencies( ai-server ${DEPENDENCIES} )
target_link_libraries( ai-server ${LIBRARIES} ${DEPENDENCIES} )
set_target_properties( ai-server PROPERTIES COMPILE_FLAGS "${DEFINITIONS}" )
if( INSTALL_COMPONENT_RUNTIME )
	cpack_add_component( Runtime_aiserver DESCRIPTION "ai-server" DISPLAY_NAME "ai-server" GROUP Runtime )
	install( TARGETS ai-server
		DESTINATION "."
		COMPONENT Runtime_aiserver )
endif( INSTALL_COMPONENT_RUNTIME )
set( TARGET_LIST ${TARGET_LIST} ai-server  CACHE INTERNAL "" )
message( STATUS "Creating target ai-server - done" )
endif( BUILD_SERVERS )
