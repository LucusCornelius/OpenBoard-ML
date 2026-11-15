
# the line between the participants: relay connection, encryption

HEADERS      += src/collab/UBCollabChannel.h \
                src/collab/UBCollabRelayChannel.h

SOURCES      += src/collab/UBCollabRelayChannel.cpp



# the shared board: what is sent about a board, and the interface of a session

HEADERS      += src/collab/UBCollabBoardLink.h \
                src/collab/UBCollabDocumentSync.h \
                src/collab/UBCollabItems.h \
                src/collab/UBCollabLoopbackChannel.h \
                src/collab/UBCollabProtocol.h \
                src/collab/UBCollabSession.h \
                src/collab/UBCollabSnapshot.h \
                src/collab/UBCollabSvgFilter.h \
                src/collab/UBCollabWire.h

SOURCES      += src/collab/UBCollabBoardLink.cpp \
                src/collab/UBCollabDocumentSync.cpp \
                src/collab/UBCollabItems.cpp \
                src/collab/UBCollabLoopbackChannel.cpp \
                src/collab/UBCollabProtocol.cpp \
                src/collab/UBCollabSession.cpp \
                src/collab/UBCollabSnapshot.cpp \
                src/collab/UBCollabSvgFilter.cpp \
                src/collab/UBCollabWire.cpp
