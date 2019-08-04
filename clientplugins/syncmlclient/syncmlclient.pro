TARGET = syncml-client
DEPENDPATH += .
INCLUDEPATH += . ../../syncmlcommon

CONFIG += link_pkgconfig
PKGCONFIG = buteosyncfw5 buteosyncml5 Qt5SystemInfo accounts-qt5 libsignon-qt5 KF5BluezQt
LIBS += -lsyncmlcommon5
LIBS += -L../../syncmlcommon

QT += dbus sql network
QT -= gui

VER_MAJ = 1
VER_MIN = 0
VER_PAT = 0

#DEFINES += BUTEO_ENABLE_DEBUG
HEADERS += SyncMLClient.h BTConnection.h
SOURCES += SyncMLClient.cpp BTConnection.cpp

TEMPLATE = lib
CONFIG += plugin
target.path = $$[QT_INSTALL_LIBS]/buteo-plugins-qt5/oopp

OTHER_FILES += xml/* \
               xml/sync/* \
               xml/service/* \
               xml/storage/*

QMAKE_CXXFLAGS = -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

QMAKE_CLEAN += $(TARGET) $(TARGET0) $(TARGET1) $(TARGET2)
QMAKE_CLEAN += $(OBJECTS_DIR)/*.gcda $(OBJECTS_DIR)/*.gcno $(OBJECTS_DIR)/*.gcov $(OBJECTS_DIR)/moc_*

client.path = /etc/buteo/profiles/client
client.files = xml/syncml.xml

sync.path = /etc/buteo/profiles/sync
sync.files = xml/sync/*

service.path = /etc/buteo/profiles/service
service.files = xml/service/*

storage.path = /etc/buteo/profiles/storage
storage.files = xml/storage/*

INSTALLS += target client sync service storage
