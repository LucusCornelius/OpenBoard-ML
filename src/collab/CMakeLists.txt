# the line between the participants: relay connection, encryption
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabChannel.h
    UBCollabCrypto.cpp
    UBCollabCrypto.h
    UBCollabRelayChannel.cpp
    UBCollabRelayChannel.h
    UBCollabRelayTuning.h
    UBCollabWebSocket.cpp
    UBCollabWebSocket.h
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
    UBCollabRules.cpp
    UBCollabRules.h
    UBCollabSession.cpp
    UBCollabSession.h
    UBCollabSnapshot.cpp
    UBCollabSnapshot.h
    UBCollabSvgFilter.cpp
    UBCollabSvgFilter.h
    UBCollabWire.cpp
    UBCollabWire.h
)
