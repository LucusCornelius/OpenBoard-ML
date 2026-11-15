# Console tests of the line between the participants of a shared board:
# the WebSocket client, the encryption and UBCollabRelayChannel, against a
# small relay in this directory or against the real relay under "wrangler dev".
#
#   mkdir -p build/collab-test && cd build/collab-test && qmake ../../tests/collab/collab-test.pro && make
#   build/collab-test/collab-test                         (everything, with the built-in relay)
#   build/collab-test/collab-test --relay http://127.0.0.1:8787 [--host-key KEY] [--grace SECONDS]
#   build/collab-test/collab-test --relay http://127.0.0.1:8787 --restart   (restart the relay when it says so)
#
# See relay/README.md for running the relay locally with "wrangler dev".

TEMPLATE = app
TARGET = collab-test
CONFIG += console c++20 warn_on
CONFIG -= app_bundle
QT = core network

ROOT = $$clean_path($$PWD/../..)
BUILD = $$ROOT/build/collab-test
DESTDIR = $$BUILD
OBJECTS_DIR = $$BUILD/objects
MOC_DIR = $$BUILD/moc

INCLUDEPATH += $$ROOT/src/collab

# the same sources as in the program
HEADERS += $$ROOT/src/collab/UBCollabChannel.h \
           $$ROOT/src/collab/UBCollabRelayChannel.h \
           $$ROOT/src/collab/UBCollabRelayTuning.h \
           $$ROOT/src/collab/UBCollabCrypto.h \
           $$ROOT/src/collab/UBCollabWebSocket.h
SOURCES += $$ROOT/src/collab/UBCollabRelayChannel.cpp \
           $$ROOT/src/collab/UBCollabCrypto.cpp \
           $$ROOT/src/collab/UBCollabWebSocket.cpp

HEADERS += UBCollabTestRelay.h
SOURCES += UBCollabTestRelay.cpp \
           main.cpp

# OpenSSL (libcrypto), found as in OpenBoard.pro
macx {
    isEmpty(MAC_LIB_PREFIX) {
        exists(/opt/homebrew/opt/openssl@3/include): MAC_LIB_PREFIX = /opt/homebrew
        else: exists(/usr/local/opt/openssl@3/include): MAC_LIB_PREFIX = /usr/local
        else: MAC_LIB_PREFIX = /opt/local
    }
    exists($$MAC_LIB_PREFIX/opt/openssl@3/include) {
        INCLUDEPATH += $$MAC_LIB_PREFIX/opt/openssl@3/include
        LIBS += -L$$MAC_LIB_PREFIX/opt/openssl@3/lib
    } else {
        INCLUDEPATH += $$MAC_LIB_PREFIX/include
        LIBS += -L$$MAC_LIB_PREFIX/lib
    }
    LIBS += -lcrypto
}
win32 {
    isEmpty(WIN_LIB_PREFIX): WIN_LIB_PREFIX = $$(WIN_LIB_PREFIX)
    INCLUDEPATH += $$WIN_LIB_PREFIX/include
    LIBS += -L$$WIN_LIB_PREFIX/lib -llibcrypto
}
unix:!macx {
    LIBS += -lcrypto
}
