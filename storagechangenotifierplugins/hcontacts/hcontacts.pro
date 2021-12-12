TEMPLATE = lib
TARGET = hcontacts-changenotifier

DEPENDPATH += .

CONFIG += link_pkgconfig plugin link_pkgconfig

INCLUDEPATH += ../../syncmlcommon

PKGCONFIG += buteosyncfw5 Qt5Contacts
target.path = $$[QT_INSTALL_LIBS]/buteo-plugins-qt5

VER_MAJ = 1
VER_MIN = 0
VER_PAT = 0

QT -= gui

HEADERS += ContactsChangeNotifierPlugin.h \
           ContactsChangeNotifier.h

SOURCES += ContactsChangeNotifierPlugin.cpp \
           ContactsChangeNotifier.cpp

QMAKE_CXXFLAGS = -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

QMAKE_CLEAN += $(TARGET)

INSTALLS += target
