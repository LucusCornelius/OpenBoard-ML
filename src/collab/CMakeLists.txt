# the line between the participants: relay connection, encryption
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabChannel.h
    UBCollabRelayChannel.cpp
    UBCollabRelayChannel.h
)



# the shared board: what is sent about a board, and the interface of a session
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabItems.cpp
    UBCollabItems.h
    UBCollabLoopbackChannel.cpp
    UBCollabLoopbackChannel.h
    UBCollabProtocol.cpp
    UBCollabProtocol.h
    UBCollabSvgFilter.cpp
    UBCollabSvgFilter.h
    UBCollabWire.cpp
    UBCollabWire.h
)
