QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    widget.cpp \
    sessionclient.cpp \
    sessioncard.cpp \
    addsessiondialog.cpp \
    windowactivator.cpp \
    floatingball.cpp \
    soundmanager.cpp \
    alertpopup.cpp

HEADERS += \
    widget.h \
    sessionclient.h \
    sessioncard.h \
    addsessiondialog.h \
    windowactivator.h \
    floatingball.h \
    soundmanager.h \
    alertpopup.h
