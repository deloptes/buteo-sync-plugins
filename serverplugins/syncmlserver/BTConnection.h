/*
* This file is part of buteo-sync-plugins package
*
* Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
*               2019 Updated to use bluez5 by deloptes@gmail.com
*
* Author: Sateesh Kavuri <sateesh.kavuri@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* version 2.1 as published by the Free Software Foundation.
*
* This library is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
* 02110-1301 USA
*/

#ifndef BTCONNECTION_H
#define BTCONNECTION_H

#include <QObject>
#include <QMutex>

#include <adapter.h>
#include <manager.h>

#include <buteosyncml5/OBEXConnection.h>

#include "SdpProfile.h"

class BTConnection : public QObject, public DataSync::OBEXConnection
{
    Q_OBJECT
public:

    BTConnection ();

    virtual ~BTConnection ();

    /*! \sa DataSync::OBEXConnection::connect ()
     *
     */
    virtual int connect ();

    /*! \sa DataSync::OBEXConnection::isConnected ()
     *
     */
    virtual bool isConnected () const;

    /*! \sa DataSync::OBEXConnection::disconnect ()
     *
     */
    virtual void disconnect ();

    void handleSyncFinished (bool isSyncInError);

    /**
     * ! \brief BT initialization method
     */
    bool init ();

    /**
      * ! \brief BT uninitialization method
      */
    void uninit ();

signals:

    void btConnected (int fd, QString btAddr);

protected slots:

    void handleIncomingBTConnection (int fd);
    
    void handleDisconnectRequest(QString device);

    void handleBTError (int fd);

private slots:

    /*! \brief Process the result of the initBluez5ManagerJob signal
     */
    void initBluez5ManagerJobResult(BluezQt::InitManagerJob* /*job*/);

private:
    // Functions

    /**
     * ! \brief Method to open bluetooth socket
     */
    int openBTSocket (const int channelNumber);

    /**
     * ! \brief Method to close bluetooth socket
     */
    void closeBTSocket (int &fd);

    /**
     * ! \brief FD listener method
     */
    void addFdListener (const int channelNumber, int fd);

    /**
     * ! \brief Removes fd listening
     */
    void removeFdListener (const int channelNumber);

    /**
     * ! \brief Method to add service record using Bluez dbus API
     *  The service records are placed at the object path where this
     *  application registers in dbus. There is no need to call
     *  removeServiceRecords explicitly because records are removed
     *  when the application is stopped
     */
    bool addServiceRecords ();

    /**
     * ! \brief Method to roll back service record from dbus API
     */
    bool removeServiceRecords ();

    /**
     * ! \brief Method to read the service records from file
     */
    bool readSRFromFile (const QString filename, QByteArray& record);

    const QString clientServiceRecordDef () const;
    
    const QString serverServiceRecordDef () const;

private:

    int                     mServerFd;

    int                     mClientFd;

    int                     mPeerSocket;

    QMutex                  mMutex;

    bool                    mDisconnected;

    quint32                 mClientServiceRecordId;

    quint32                 mServerServiceRecordId;

    QSocketNotifier         *mServerReadNotifier;

    QSocketNotifier         *mServerWriteNotifier;

    QSocketNotifier         *mServerExceptionNotifier;

    QSocketNotifier         *mClientReadNotifier;

    QSocketNotifier         *mClientWriteNotifier;

    QSocketNotifier         *mClientExceptionNotifier;

    bool                    mServerFdWatching;

    bool                    mClientFdWatching;

    BluezQt::Manager        *btManager;

    SdpProfile              *clientProfile;

    SdpProfile              *serverProfile;


};

#endif // BTCONNECTION_H
