/*
* This file is part of buteo-sync-plugins package
*
* Copyright (C) 2010 Nokia Corporation. All rights reserved.
*               2019 Updated to use bluez5 by deloptes@gmail.com
*
* Contact: Sateesh Kavuri <sateesh.kavuri@nokia.com>
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following disclaimer in the documentation
* and/or other materials provided with the distribution.
* Neither the name of Nokia Corporation nor the names of its contributors may
* be used to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include "BTConnection.h"

#include <unistd.h>
#include <QtDBus>
#include <QDBusConnection>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>

#include <adapter.h>
#include <device.h>
#include <initmanagerjob.h>
#include <pendingcall.h>

#include "SyncMLPluginLogging.h"

BTConnection::BTConnection()
 : iFd( -1 )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // Initialize BluezQt
    btManager = new BluezQt::Manager(this);
    if (btManager != 0) {
        BluezQt::InitManagerJob *initJob = btManager->init();
        initJob->start();
        QObject::connect(initJob, &BluezQt::InitManagerJob::result,
                this, &BTConnection::initBluez5ManagerJobResult/*,
                    Qt::QueuedConnection*/);
        qCDebug(lcSyncMLPlugin) << "[Clnt]BTConnection manager init started";

        QTime dieTime= QTime::currentTime().addSecs(2);
        while (!btManager->isOperational() && (QTime::currentTime() < dieTime))
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    else
    {
        qCCritical(lcSyncMLPlugin) << "[Clnt]BTConnection manager init failed";
    }
}

BTConnection::~BTConnection()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    disconnect();
}

void BTConnection::initBluez5ManagerJobResult(BluezQt::InitManagerJob* job)
{

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if (job->error()) {
        qCCritical(lcSyncMLPlugin) << "[Clnt]BTConnection manager init error: " << job->errorText();
        return;
    }

    // @todo do we need this
    Q_ASSERT(job->manager() == btManager);

    if (!btManager->isBluetoothOperational()) {
        if (btManager->isBluetoothBlocked())
            qCWarning(lcSyncMLPlugin) << "[Clnt]BTConnection manager init failed (adapter is blocked)";
        else
            qCCritical(lcSyncMLPlugin) << "[Clnt]BTConnection manager init failed (not operational)";
        return;
    }

    qCDebug(lcSyncMLPlugin) << "[Clnt]BTConnection manager init done";
}

void BTConnection::setConnectionInfo( const QString& aBTAddress,
                                      const QString& aServiceUUID )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    iBTAddress = aBTAddress;
    iServiceUUID = aServiceUUID;
}

int BTConnection::connect()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if( iFd != -1 ) {
        qCDebug(lcSyncMLPlugin) << "[Clnt]BTConnection: Using existing connection";
        return iFd;
    }

    iDevice = connectDevice( iBTAddress, iServiceUUID );

    if( iDevice.isEmpty() ) {
        qCCritical(lcSyncMLPlugin) << "Could not connect to device" << iBTAddress << ", aborting";
        return -1;
    }

    // HACK: In Sailfish, sometimes, opening the device
    // immediately after the bluetooth connect fails and works only
    // if some delay is introduced.
    // Since a plugin runs in a separate thread/process (incase of oop)
    // it is okay to introduce some delay before the open. We will use
    // a retry count of 3 to open the connection and finally giveup
    // otherwise
    int retryCount = 3;
    do {
        iFd = open( iDevice.toLatin1().constData(), O_RDWR | O_NOCTTY | O_SYNC );
        if (iFd > 0) break;
        QThread::msleep (100); // Sleep for 100msec before trying again
    } while ((--retryCount > 0) && (iFd == -1));

    if( iFd == -1 ) {
        qCCritical(lcSyncMLPlugin) << "[Clnt]BTConnection: Could not open file descriptor of the connection, aborting";
        disconnectDevice( iBTAddress, iDevice );
        return -1;
    }

    fdRawMode( iFd );

    return iFd;
}

bool BTConnection::isConnected() const
{
    if( iFd != -1 )
    {
        return true;
    }
    else
    {
        return false;
    }

}

void BTConnection::disconnect()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if( iFd != -1 ) {
        close( iFd );
        iFd = -1;
    }

    if( !iDevice.isEmpty() ) {
        disconnectDevice( iBTAddress, iDevice );
    }

}

QString BTConnection::connectDevice( const QString& aBTAddress, const QString& aServiceUUID )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    BluezQt::DevicePtr dev = btManager->deviceForAddress(aBTAddress);
    if (!dev) {
        qCWarning(lcSyncMLPlugin) << "[Clnt]Device query failed for addr: " << aBTAddress;
        return QString();
    }

    QStringList props = dev->uuids();
    if (!props.contains(aServiceUUID), Qt::CaseInsensitive) {
        qCWarning(lcSyncMLPlugin) << "[Clnt]Device does not provide SyncML Service: " << aServiceUUID;
        qCWarning(lcSyncMLPlugin) << "[Clnt]Device properties: " << props.join(", ");
        return QString();
    }

    BluezQt::PendingCall *call = dev->connectProfile(aServiceUUID);
    call->waitForFinished();

    if (call->error()) {
        qCCritical(lcSyncMLPlugin) << "[Clnt]Could not connect to device "
                << aBTAddress << " with service uuid " << aServiceUUID ;
        qCCritical(lcSyncMLPlugin) << "[Clnt]Reason:" <<  call->errorText();
        return QString();
    }

    qCDebug(lcSyncMLPlugin) << "Device connected:" << aBTAddress;

    // FIXME
    // not sure what this call was returning in BlueZ4
    //    QDBusReply<QString> stringReply = serialInterface.call( QLatin1String( CONNECT ), aServiceUUID );
    // I think it is nice to return the device name
    return dev->name ();
}

void BTConnection::disconnectDevice( const QString& aBTAddress, const QString& aDevice )
{
    Q_UNUSED(aDevice);
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    BluezQt::DevicePtr dev = btManager->deviceForAddress(aBTAddress);
    if (!dev)
    {
        qCWarning(lcSyncMLPlugin) << "[Clnt]Device query failed for addr: " << aBTAddress;
        return;
    }

    BluezQt::PendingCall *call = dev->disconnectFromDevice();
    call->waitForFinished();
    int err = call->error() ;
    if ( err && err != BluezQt::PendingCall::NotConnected ) {
        qCCritical(lcSyncMLPlugin) << "[Clnt]Could not diconnect from device " << aBTAddress;
        qCCritical(lcSyncMLPlugin) << "[Clnt]Reason:" <<  call->errorText();
    }

    qCDebug(lcSyncMLPlugin) << "Device disconnected:" << aBTAddress;

    iDevice.clear();
}

bool BTConnection::fdRawMode( int aFD )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    struct termios mode;

    if (tcgetattr(aFD, &mode)) {
        return false;
    }

    cfmakeraw(&mode);

    if (tcsetattr(aFD, TCSADRAIN, &mode)) {
        return false;
    }

    return true;
}
