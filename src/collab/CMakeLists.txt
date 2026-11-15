# the line between the participants: relay connection, encryption
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabChannel.h
    UBCollabRelayChannel.cpp
    UBCollabRelayChannel.h
)



# the shared board: what is sent about a board, and the interface of a session
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabBoardLink.cpp
    UBCollabBoardLink.h
    UBCollabController.cpp
    UBCollabController.h
    UBCollabCursorOverlay.cpp
    UBCollabCursorOverlay.h
    UBCollabDocumentSync.cpp
    UBCollabDocumentSync.h
    UBCollabItems.cpp
    UBCollabItems.h
    UBCollabLoopbackChannel.cpp
    UBCollabLoopbackChannel.h
    UBCollabPanel.cpp
    UBCollabPanel.h
    UBCollabProtocol.cpp
    UBCollabProtocol.h
    UBCollabSession.cpp
    UBCollabSession.h
    UBCollabSnapshot.cpp
    UBCollabSnapshot.h
    UBCollabSvgFilter.cpp
    UBCollabSvgFilter.h
    UBCollabWire.cpp
    UBCollabWire.h
)

# the glyph of the toolbar button, under :/images/glass with the other glyphs
if(Qt5_VERSION AND Qt5_VERSION VERSION_LESS "5.15")
    qt5_add_resources(OPENBOARD_COLLAB_RESOURCES collab.qrc)
else()
    qt_add_resources(OPENBOARD_COLLAB_RESOURCES collab.qrc)
endif()

target_sources(${PROJECT_NAME} PRIVATE ${OPENBOARD_COLLAB_RESOURCES})
