TARGET = syncml-server
CONFIG += link_pkgconfig
PKGCONFIG += glib-2.0 buteosyncfw5 buteosyncml5 Qt5SystemInfo KF5BluezQt

INCLUDEPATH += . ../../syncmlcommon
LIBS += -L../../syncmlcommon
LIBS += -lsyncmlcommon5

QT       -= gui

VER_MAJ = 1
VER_MIN = 0
VER_PAT = 0

QMAKE_CXXFLAGS = -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

QMAKE_CLEAN += $(TARGET) $(TARGET0) $(TARGET1) $(TARGET2)
QMAKE_CLEAN += $(OBJECTS_DIR)/*.gcda $(OBJECTS_DIR)/*.gcno $(OBJECTS_DIR)/*.gcov $(OBJECTS_DIR)/moc_*

SOURCES += SyncMLServer.cpp \
    USBConnection.cpp \
    BTConnection.cpp  \
    SdpProfile.cpp

HEADERS += SyncMLServer.h\
    syncmlserver_global.h \
    USBConnection.h \
    BTConnection.h \
    SdpProfile.h

OTHER_FILES += xml/*

TEMPLATE = lib
CONFIG += plugin
target.path = $$[QT_INSTALL_LIBS]/buteo-plugins-qt5/oopp/
DEFINES += SYNCMLSERVER_LIBRARY

sync.path = /etc/buteo/profiles/server
sync.files = xml/syncml.xml

template.path = /etc/buteo/profiles/sync
template.files = xml/bt_template.xml

btsrs.path = /etc/buteo/plugins/syncmlserver
btsrs.files = xml/syncml_server_sdp_record.xml xml/syncml_client_sdp_record.xml

INSTALLS += target sync btsrs template
