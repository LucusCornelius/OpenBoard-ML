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
