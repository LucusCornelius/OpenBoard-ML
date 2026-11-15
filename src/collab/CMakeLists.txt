# the line between the participants: relay connection, encryption
target_sources(${PROJECT_NAME} PRIVATE
    UBCollabChannel.h
    UBCollabRelayChannel.cpp
    UBCollabRelayChannel.h
)



# the shared board: what is sent about a board, and the interface of a session
