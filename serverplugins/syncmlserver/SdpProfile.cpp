/*
 * Copyright (C) 2019 Emanoil Kotsev <deloptes@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
#include <unistd.h>


#include <manager.h>
#include <initmanagerjob.h>
#include <pendingcall.h>

#include "SyncMLPluginLogging.h"

#include "SdpProfile.h"

SdpProfile::SdpProfile(BluezQt::Profile::LocalRole role, const QString &sdp, QObject *parent)
    : BluezQt::Profile(parent),mSocket(0), mDeviceAddress(QString()),
      mUuid(QString()), mObjectPath(QDBusObjectPath())
{

    int channel = 0;
    switch(role) {
    case BluezQt::Profile::ServerRole:
        mUuid = SERVER_SDP_UUID;
        mObjectPath = QDBusObjectPath(SERVER_DBUS_PATH);
        channel = BT_SERVER_CHANNEL;
        break;
    case BluezQt::Profile::ClientRole:
        mUuid = CLIENT_SDP_UUID;
        mObjectPath = QDBusObjectPath(CLIENT_DBUS_PATH);
        channel = BT_CLIENT_CHANNEL;
        break;
    default:
        qCCritical(lcSyncMLPlugin) << "A valid role for the profile is missing";
    }

    setLocalRole(role);
    setChannel(channel);
    setServiceRecord(sdp);
    setRequireAuthentication(true);
    setRequireAuthorization(false);

}

SdpProfile::~SdpProfile()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
//
//    if (mSocket->isOpen())
//        mSocket->close();
}

QDBusObjectPath SdpProfile::objectPath() const
{
    return mObjectPath;
}

QString SdpProfile::uuid() const
{
    return mUuid;
}

void SdpProfile::newConnection(BluezQt::DevicePtr device, const QDBusUnixFileDescriptor &fd, const QVariantMap &properties, const BluezQt::Request<> &request)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Connect fd" << fd.fileDescriptor() << device->address() << properties;

    mDeviceAddress = device->address();
    mDeviceProperties = properties;

    mSocket = createSocket(fd);
    if (!mSocket->isValid()) {
        qCCritical(lcSyncMLPlugin) << "Invalid socket";
        request.cancel();
        return;
    }
    emit incomingBTConnection(mSocket->socketDescriptor());
    request.accept();
}

void SdpProfile::requestDisconnection(BluezQt::DevicePtr device, const BluezQt::Request<> &request)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    // Device device is disconnecting
    qCDebug(lcSyncMLPlugin) << "Disconnect" << device->address();

    if (mSocket->isOpen())
        mSocket->close();

    emit disconnectRequest(device->address());
    request.accept();
}

void SdpProfile::release()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    mSocket.clear();
}

int SdpProfile::socketFd()
{
    return mSocket->socketDescriptor();
}

QString SdpProfile::deviceAddress()
{
    return mDeviceAddress;
}

QVariantMap SdpProfile::deviceProperties()
{
    return mDeviceProperties;
}
