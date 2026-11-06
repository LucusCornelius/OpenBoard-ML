target_sources(${PROJECT_NAME} PRIVATE
    UBBackgroundLoader.cpp
    UBBackgroundLoader.h
    UBBase32.cpp
    UBBase32.h
    UBBlockingBuffer.cpp
    UBBlockingBuffer.h
    UBCoreGraphicsScene.cpp
    UBCoreGraphicsScene.h
    UBCryptoUtils.cpp
    UBCryptoUtils.h
    UBFileSystemUtils.cpp
    UBFileSystemUtils.h
    UBGeometryUtils.cpp
    UBGeometryUtils.h
    UBPlatformUtils.cpp
    UBPlatformUtils.h
    UBStringUtils.cpp
    UBStringUtils.h
    UBVersion.cpp
    UBVersion.h
)

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    target_sources(${PROJECT_NAME} PRIVATE
        UBDesktopPortal.cpp
        UBDesktopPortal.h
        UBPipewireSink.cpp
        UBPipewireSink.h
        UBPlatformUtils_linux.cpp
    )
endif()
