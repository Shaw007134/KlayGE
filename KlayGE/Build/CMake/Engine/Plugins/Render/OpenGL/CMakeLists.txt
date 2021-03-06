SET(LIB_NAME KlayGE_RenderEngine_OpenGL)

SET(OPENGL_RE_SOURCE_FILES
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLFence.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLFrameBuffer.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLGraphicsBuffer.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLMapping.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLQuery.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderEngine.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderFactory.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderLayout.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderStateObject.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderView.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLRenderWindow.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLShaderObject.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLTexture.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLTexture1D.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLTexture2D.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLTexture3D.cpp
	${KLAYGE_PROJECT_DIR}/Plugins/Src/Render/OpenGL/OGLTextureCube.cpp
)

SET(OPENGL_RE_HEADER_FILES
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLFence.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLFrameBuffer.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLGraphicsBuffer.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLMapping.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLQuery.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderEngine.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderFactory.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderLayout.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderStateObject.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderView.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLRenderWindow.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLShaderObject.hpp
	${KLAYGE_PROJECT_DIR}/Plugins/Include/KlayGE/OpenGL/OGLTexture.hpp
)

SOURCE_GROUP("Source Files" FILES ${OPENGL_RE_SOURCE_FILES})
SOURCE_GROUP("Header Files" FILES ${OPENGL_RE_HEADER_FILES})

INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../KFL/include)
INCLUDE_DIRECTORIES(${KLAYGE_PROJECT_DIR}/Core/Include)
INCLUDE_DIRECTORIES(${KLAYGE_PROJECT_DIR}/Plugins/Include)
INCLUDE_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../glloader/include)
if(KLAYGE_IS_DEV_PLATFORM)
	INCLUDE_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../DXBC2GLSL/Include)
endif()
LINK_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../KFL/lib/${KLAYGE_PLATFORM_NAME})
IF(KLAYGE_PLATFORM_DARWIN OR KLAYGE_PLATFORM_LINUX)
	LINK_DIRECTORIES(${KLAYGE_BIN_DIR})
ELSE()
	LINK_DIRECTORIES(${KLAYGE_OUTPUT_DIR})
ENDIF()
if(KLAYGE_PLATFORM_WINDOWS OR (KLAYGE_PREFERRED_LIB_TYPE STREQUAL "STATIC"))
	LINK_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../glloader/lib/${KLAYGE_PLATFORM_NAME})
endif()
if(KLAYGE_IS_DEV_PLATFORM)
	LINK_DIRECTORIES(${KLAYGE_PROJECT_DIR}/../DXBC2GLSL/lib/${KLAYGE_PLATFORM_NAME})
endif()

ADD_LIBRARY(${LIB_NAME} SHARED
	${OPENGL_RE_SOURCE_FILES} ${OPENGL_RE_HEADER_FILES}
)
ADD_DEPENDENCIES(${LIB_NAME} ${KLAYGE_CORELIB_NAME} glloader)
if(KLAYGE_IS_DEV_PLATFORM)
	add_dependencies(${LIB_NAME} DXBC2GLSLLib)
endif()

SET_TARGET_PROPERTIES(${LIB_NAME} PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY ${KLAYGE_OUTPUT_DIR}
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${KLAYGE_OUTPUT_DIR}
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${KLAYGE_OUTPUT_DIR}
	ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${KLAYGE_OUTPUT_DIR}
	ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${KLAYGE_OUTPUT_DIR}
	RUNTIME_OUTPUT_DIRECTORY ${KLAYGE_BIN_DIR}/Render
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${KLAYGE_BIN_DIR}/Render
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${KLAYGE_BIN_DIR}/Render
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${KLAYGE_BIN_DIR}/Render
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${KLAYGE_BIN_DIR}/Render
	LIBRARY_OUTPUT_DIRECTORY ${KLAYGE_BIN_DIR}/Render
	LIBRARY_OUTPUT_DIRECTORY_DEBUG ${KLAYGE_BIN_DIR}/Render
	LIBRARY_OUTPUT_DIRECTORY_RELEASE ${KLAYGE_BIN_DIR}/Render
	LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${KLAYGE_BIN_DIR}/Render
	LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${KLAYGE_BIN_DIR}/Render
	PROJECT_LABEL ${LIB_NAME}
	DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX}
	OUTPUT_NAME ${LIB_NAME}${KLAYGE_OUTPUT_SUFFIX}
	FOLDER "KlayGE/Engine/Plugins/Render"
)

KLAYGE_ADD_PRECOMPILED_HEADER(${LIB_NAME} "${KLAYGE_PROJECT_DIR}/Core/Include/KlayGE/KlayGE.hpp")

if(KLAYGE_IS_DEV_PLATFORM)
	target_link_libraries(${LIB_NAME}
		debug DXBC2GLSLLib${KLAYGE_OUTPUT_SUFFIX}_d optimized DXBC2GLSLLib${KLAYGE_OUTPUT_SUFFIX}
	)
endif()
TARGET_LINK_LIBRARIES(${LIB_NAME}
	debug glloader${KLAYGE_OUTPUT_SUFFIX}_d optimized glloader${KLAYGE_OUTPUT_SUFFIX}
	debug KlayGE_Core${KLAYGE_OUTPUT_SUFFIX}_d optimized KlayGE_Core${KLAYGE_OUTPUT_SUFFIX}
	debug KFL${KLAYGE_OUTPUT_SUFFIX}_d optimized KFL${KLAYGE_OUTPUT_SUFFIX}
)

ADD_DEPENDENCIES(AllInEngine ${LIB_NAME})
