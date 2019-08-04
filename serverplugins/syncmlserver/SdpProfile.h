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

#ifndef SDPPROFILE_H
#define SDPPROFILE_H

#include <QLocalSocket>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>

#include "profile.h"
#include "device.h"

const QString CLIENT_SDP_UUID ("00000002-0000-1000-8000-0002ee000002");
const QString SERVER_SDP_UUID ("00000001-0000-1000-8000-0002ee000001");
const QString CLIENT_DBUS_PATH ("/org/deloptes/syncml/client");
const QString SERVER_DBUS_PATH ("/org/deloptes/syncml/server");
const int BT_SERVER_CHANNEL = 26;
const int BT_CLIENT_CHANNEL = 25;

class SdpProfile : public BluezQt::Profile
{
    Q_OBJECT

public:
    explicit SdpProfile(BluezQt::Profile::LocalRole role, const QString &sdp, QObject *parent = 0);

    virtual ~SdpProfile();

    // implemented from Profile1
    QDBusObjectPath objectPath() const;

    QString uuid() const;

    /*! \brief Need to implement, because current Profile does not support this service
     * for details see https://api.kde.org/frameworks/bluez-qt/html/classBluezQt_1_1Profile.html
     */
    void newConnection(BluezQt::DevicePtr device, const QDBusUnixFileDescriptor &fd, const QVariantMap &properties, const BluezQt::Request<> &request);

    /*! \brief Need to implement, because current Profile does not support this service
     * for details see https://api.kde.org/frameworks/bluez-qt/html/classBluezQt_1_1Profile.html
     */
    void requestDisconnection(BluezQt::DevicePtr device, const BluezQt::Request<> &request);

    /*! \brief Need to implement, because current Profile does not support this service
     * for details see https://api.kde.org/frameworks/bluez-qt/html/classBluezQt_1_1Profile.html
     */
    void release();

    // other methods
    int socketFd();

    QString deviceAddress();

    QVariantMap  deviceProperties();

signals:
    void incomingBTConnection(int fd);

    void disconnectRequest(QString address);

private:
    QSharedPointer<QLocalSocket>    mSocket;
    QString                         mDeviceAddress;
    QVariantMap                     mDeviceProperties;
    QString                         mUuid;
    QDBusObjectPath                 mObjectPath;
};

#endif // SDPPROFILE_H
