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

#include <LogMacros.h>
#include <stdint.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <profile.h>
#include <initmanagerjob.h>
#include <pendingcall.h>

#include "BTConnection.h"

const QString BTSRS_PATH ("/etc/buteo/plugins/syncmlserver");
const QString CLIENT_BT_SR_FILE ("syncml_client_sdp_record.xml");
const QString SERVER_BT_SR_FILE ("syncml_server_sdp_record.xml");

const int BT_RFCOMM_PROTO = 3;
const int RFCOMM_LM = 0x03;
const int SOL_RFCOMM = 18;
const int RFCOMM_LM_SECURE = 0x0200;

typedef struct {
    uint8_t b[6];
} __attribute__((packed)) btbdaddr_t;

struct sockaddr_rc {
    sa_family_t     rc_family;
    btbdaddr_t      rc_bdaddr;
    uint8_t         rc_channel;
};

BTConnection::BTConnection() :
   mServerFd (-1), mClientFd (-1), mPeerSocket (-1), mMutex (QMutex::Recursive),
   mDisconnected (true), mClientServiceRecordId (-1), mServerServiceRecordId (-1),
   mServerReadNotifier (0), mServerWriteNotifier (0), mServerExceptionNotifier (0),
   mClientReadNotifier (0), mClientWriteNotifier (0), mClientExceptionNotifier (0),
   mServerFdWatching (false), mClientFdWatching (false),
   btManager(0), clientProfile(0), serverProfile(0)
{
    FUNCTION_CALL_TRACE;

}

BTConnection::~BTConnection ()
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG ("BTConnection::~BTConnection ");

    if (mServerReadNotifier) {
        delete mServerReadNotifier;
        mServerReadNotifier = 0;
    }

    if (mServerWriteNotifier) {
        delete mServerWriteNotifier;
        mServerWriteNotifier = 0;
    }

    if (mServerExceptionNotifier) {
        delete mServerExceptionNotifier;
        mServerExceptionNotifier = 0;
    }

    if (mClientReadNotifier) {
        delete mClientReadNotifier;
        mClientReadNotifier = 0;
    }

    if (mClientWriteNotifier) {
        delete mClientWriteNotifier;
        mClientWriteNotifier = 0;
    }

    if (mClientExceptionNotifier) {
        delete mClientExceptionNotifier;
        mClientExceptionNotifier = 0;
    }

    if (btManager) {
        removeServiceRecords ();
    }

    if (serverProfile) {
        delete serverProfile;
        serverProfile = 0;
    }

    if (clientProfile) {
        delete clientProfile;
        clientProfile = 0;
    }

    if (btManager) {
        delete btManager;
        btManager = 0;
    }

}

void BTConnection::initBluez5ManagerJobResult(BluezQt::InitManagerJob* job)
{
    FUNCTION_CALL_TRACE;

    if (job->error()) {
        LOG_CRITICAL("[Srvr]BTConnection manager init error: " << job->errorText());
        return;
    }

    // @todo do we need this
    Q_ASSERT(job->manager() == btManager);

    if (!btManager->isBluetoothOperational()) {
        if (btManager->isBluetoothBlocked())
            LOG_WARNING("[Srvr]BTConnection manager init failed (adapter is blocked)");
        else
            LOG_CRITICAL("[Srvr]BTConnection manager init failed (BT not operational)");
        return;
    }

    // Add client and server bluetooth sdp records
    if (!addServiceRecords()) {
        LOG_WARNING ("[Srvr]Error in creating the SDP records");
        return;
    }

    // Open the server and client socket
    mServerFd = openBTSocket (BT_SERVER_CHANNEL);
    mClientFd = openBTSocket (BT_CLIENT_CHANNEL);

    if (mServerFd == -1 || mClientFd == -1) {
        LOG_WARNING ("[Srvr]Error in opening BT client or server socket");
        removeServiceRecords();
        return;
    }

    addFdListener (BT_SERVER_CHANNEL, mServerFd);
    addFdListener (BT_CLIENT_CHANNEL, mClientFd);

    LOG_INFO ("[Srvr]BTConnection manager init done");
}

int
BTConnection::connect ()
{
    FUNCTION_CALL_TRACE;
    
    return mPeerSocket;
}

bool
BTConnection::isConnected () const
{
    FUNCTION_CALL_TRACE;

    if (mPeerSocket == -1)
        return false;
    else
        return true;
}

void
BTConnection::disconnect ()
{
    FUNCTION_CALL_TRACE;
    closeBTSocket(mPeerSocket);
}

void
BTConnection::handleSyncFinished (bool isSyncInError)
{
    FUNCTION_CALL_TRACE;

    if (isSyncInError == true) {
        LOG_WARNING ("[Srvr]Sync finished with error. Resetting now.");
        // If sync error, then close the BT connection and reopen it
        removeFdListener (BT_SERVER_CHANNEL);
        removeFdListener (BT_CLIENT_CHANNEL);
        closeBTSocket (mServerFd);
        closeBTSocket (mClientFd);
        openBTSocket (BT_SERVER_CHANNEL);
        openBTSocket (BT_CLIENT_CHANNEL);

        addFdListener (BT_SERVER_CHANNEL, mServerFd);
        addFdListener (BT_CLIENT_CHANNEL, mClientFd);
    } else {
        // No errors during sync. Add the fd listener
        LOG_DEBUG ("[Srvr]Sync successfully finished.");
        addFdListener (BT_SERVER_CHANNEL, mServerFd);
        addFdListener (BT_CLIENT_CHANNEL, mClientFd);
    }
}

int
BTConnection::openBTSocket (const int channelNumber)
{
    FUNCTION_CALL_TRACE;

    int sock = socket (AF_BLUETOOTH, SOCK_STREAM, BT_RFCOMM_PROTO);
    if (sock < 0) {
        LOG_WARNING ("[Srvr]Unable to open bluetooth socket");
        return -1;
    }

    int lm = RFCOMM_LM_SECURE;
    if (setsockopt (sock, SOL_RFCOMM, RFCOMM_LM, &lm, sizeof (lm)) < 0) {
        LOG_WARNING ("[Srvr]Unable to set socket options." << errno);
        return -1;
    }

    struct sockaddr_rc localAddr;
    memset (&localAddr, 0, sizeof (localAddr));
    localAddr.rc_family = AF_BLUETOOTH;
    btbdaddr_t anyAddr = {{0, 0, 0, 0, 0, 0}}; // bind to any local bluetooth address
    localAddr.rc_channel = channelNumber;

    memcpy (&localAddr.rc_bdaddr, &anyAddr, sizeof (btbdaddr_t));

    // Bind the socket
    if (bind (sock, (struct sockaddr*)&localAddr, sizeof (localAddr)) < 0) {
        LOG_WARNING ("[Srvr]Unable to bind to local address");
        return -1;
    }

    // Listen for incoming connections
    if (listen (sock, 1) < 0) { // We allow a max of 1 connection per SyncML session
        LOG_WARNING ("[Srvr]Error while starting listening");
        return -1;
    }

    // Set the socket into non-blocking mode
    long flags = fcntl (sock, F_GETFL);
    if (flags < 0) {
        LOG_WARNING ("[Srvr]Error while getting flags for socket");
    } else {
        flags |= O_NONBLOCK;
        if (fcntl (sock, F_SETFL, flags) < 0) {
            LOG_WARNING ("[Srvr]Error while setting socket into non-blocking mode");
        }
    }

    LOG_DEBUG ("[Srvr]Opened BT socket with fd " << sock << " for channel " << channelNumber);
    return sock;
}

void
BTConnection::closeBTSocket (int& fd)
{
    FUNCTION_CALL_TRACE;

    if (fd != -1) {
        close (fd);
        fd = -1;
    }
}

void
BTConnection::addFdListener (const int channelNumber, int fd)
{
    FUNCTION_CALL_TRACE;

    if ((channelNumber == BT_SERVER_CHANNEL) && (mServerFdWatching == false) && (fd != -1)) {
        mServerReadNotifier = new QSocketNotifier (fd, QSocketNotifier::Read);
        mServerWriteNotifier = new QSocketNotifier (fd, QSocketNotifier::Write);
        mServerExceptionNotifier = new QSocketNotifier (fd, QSocketNotifier::Exception);

        mServerReadNotifier->setEnabled (true);
        mServerWriteNotifier->setEnabled (true);
        mServerExceptionNotifier->setEnabled (true);

        QObject::connect (mServerReadNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::connect (mServerWriteNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::connect (mServerExceptionNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleBTError(int)));

        LOG_DEBUG ("[Srvr]Added listener for server socket " << fd);
        mServerFdWatching = true;
    }

    if ((channelNumber == BT_CLIENT_CHANNEL) && (mClientFdWatching == false) && (fd != -1)) {
        mClientReadNotifier = new QSocketNotifier (fd, QSocketNotifier::Read);
        mClientWriteNotifier = new QSocketNotifier (fd, QSocketNotifier::Write);
        mClientExceptionNotifier = new QSocketNotifier (fd, QSocketNotifier::Exception);

        mClientReadNotifier->setEnabled (true);
        mClientWriteNotifier->setEnabled (true);
        mClientExceptionNotifier->setEnabled (true);

        QObject::connect (mClientReadNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::connect (mClientWriteNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::connect (mClientExceptionNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleBTError(int)));

        LOG_DEBUG ("[Srvr]Added listener for client socket " << fd);
        mClientFdWatching = true;
    }

    mDisconnected = false;
}

void
BTConnection::removeFdListener (const int channelNumber)
{
    FUNCTION_CALL_TRACE;

    if (channelNumber == BT_SERVER_CHANNEL) {
        mServerReadNotifier->setEnabled (false);
        mServerWriteNotifier->setEnabled (false);
        mServerExceptionNotifier->setEnabled (false);

        QObject::disconnect (mServerReadNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::disconnect (mServerWriteNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::disconnect (mServerExceptionNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleBTError(int)));

        mServerFdWatching = false;

    } else if (channelNumber == BT_CLIENT_CHANNEL) {
        mClientReadNotifier->setEnabled (false);
        mClientWriteNotifier->setEnabled (false);
        mClientExceptionNotifier->setEnabled (false);

        QObject::disconnect (mClientReadNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::disconnect (mClientWriteNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleIncomingBTConnection(int)));
        QObject::disconnect (mClientExceptionNotifier, SIGNAL (activated(int)),
                        this, SLOT (handleBTError (int)));

        mClientFdWatching = false;
    }
//    @todo do we need it here - was it forgotten or intentionally missing
//    see above addFdListener - it looks like it is not used at all
//    mDisconnected = true;
    LOG_DEBUG ("[Srvr]Removed listener for channel" << channelNumber);
}

void
BTConnection::handleDisconnectRequest(QString device)
{
    Q_UNUSED(device);
    // do something with device

    mPeerSocket = -1;
}

void
BTConnection::handleIncomingBTConnection (int fd)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG ("Incoming BT connection fd(" << fd << ")");

    mPeerSocket = dup(fd);

    QString btAddr;

    if (clientProfile != 0 && fd == clientProfile->socketFd()) {
        btAddr = clientProfile->deviceAddress();
        clientProfile->release();
    } else if (serverProfile != 0 && fd == serverProfile->socketFd()) {
        btAddr = serverProfile->deviceAddress();
        serverProfile->release();
    } else {
        LOG_CRITICAL ("BT Address of peer not known");
        return;
    }
    LOG_DEBUG ("Connection from device: " << btAddr);

    if (!btAddr.isEmpty()) {
        // Set the socket into non-blocking mode
        long flags = fcntl (mPeerSocket, F_GETFL);
        if (flags < 0) {
            LOG_WARNING ("[BTConn]Error while getting flags for socket");
        } else {
            flags |= O_NONBLOCK;
            if (mPeerSocket == -1 || fcntl (mPeerSocket, F_SETFL, flags) < 0) {
                LOG_WARNING ("[BTConn]Error while setting socket into non-blocking mode");
            }
        }
        emit btConnected (mPeerSocket, btAddr);
    }

    // Disable event notifier
    if (fd == mServerFd)
        removeFdListener (BT_SERVER_CHANNEL);
    else if (fd == mClientFd)
        removeFdListener (BT_CLIENT_CHANNEL);
}

void
BTConnection::handleBTError (int fd)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG ("[Srvr]Error in BT connection");
    
    // Should this be similar to USB that we close and re-init BT?
    
    // FIXME: Ugly API for fd listeners. Add a more decent way
    if (fd == mServerFd)
        removeFdListener (BT_SERVER_CHANNEL);
    else if (fd == mClientFd)
        removeFdListener (BT_CLIENT_CHANNEL);

    closeBTSocket (fd);

    if (fd == mServerFd)
        openBTSocket (BT_SERVER_CHANNEL);
    else if (fd == mClientFd)
        openBTSocket (BT_CLIENT_CHANNEL);

    if (fd == mServerFd)
        addFdListener (BT_SERVER_CHANNEL, fd);
    else if (fd == mClientFd)
        addFdListener (BT_CLIENT_CHANNEL, fd);
}

bool
BTConnection::init ()
{
    FUNCTION_CALL_TRACE;

    // Initialize BluezQt
    btManager = new BluezQt::Manager(this);
    if (btManager != 0) {
        BluezQt::InitManagerJob *initJob = btManager->init();
        initJob->start();
        QObject::connect(initJob, &BluezQt::InitManagerJob::result,
                this, &BTConnection::initBluez5ManagerJobResult/*,
                    Qt::QueuedConnection*/);
        LOG_DEBUG("[Srvr]BTConnection manager init started");
        return true;
    } else {
        LOG_CRITICAL("[Srvr]BTConnection manager init failed");
        return false;
    }
}

void BTConnection::uninit()
{
    FUNCTION_CALL_TRACE
    LOG_DEBUG ("[Srvr]BTConnection::uninit");

    // Remove listeners
    removeFdListener (BT_SERVER_CHANNEL);
    removeFdListener (BT_CLIENT_CHANNEL);

    // Remove the profiles - profiles are auto removed when app disconnects
    // from dbus, so we disconnect and clean up when BTConnection closes

    removeServiceRecords ();

    if (serverProfile) {
        QObject::disconnect (serverProfile, &SdpProfile::incomingBTConnection,
                this, &BTConnection::handleIncomingBTConnection);
        QObject::disconnect (serverProfile, &SdpProfile::disconnectRequest,
                this, &BTConnection::handleDisconnectRequest);
        delete serverProfile;
        serverProfile = 0;
    }

    if (clientProfile) {
        QObject::disconnect (clientProfile, &SdpProfile::incomingBTConnection,
                this, &BTConnection::handleIncomingBTConnection);
        QObject::disconnect (clientProfile, &SdpProfile::disconnectRequest,
                this, &BTConnection::handleDisconnectRequest);
        delete clientProfile;
        clientProfile = 0;
    }

    // Close the fd's
    closeBTSocket (mServerFd);
    closeBTSocket (mClientFd);

    // Close the peer socket
    mPeerSocket = -1;

    if (btManager) {
        delete btManager;
        btManager = 0;
    }
}

bool
BTConnection::addServiceRecords ()
{
    FUNCTION_CALL_TRACE

    // use first adapter and check if profile was already registered
    // if registered we should not try registering once again
    BluezQt::AdapterPtr adapter = btManager->adapters().first();
    LOG_DEBUG("[Srvr] adapter " << adapter->address());
    LOG_DEBUG("[Srvr] adapter uuids" << adapter->uuids());

    QByteArray clientSDP;
    if (!readSRFromFile (CLIENT_BT_SR_FILE, clientSDP)) {
        clientSDP = clientServiceRecordDef ().toLatin1 ();
    }

    clientProfile = new SdpProfile(BluezQt::Profile::ClientRole,QString(clientSDP));

    if (! adapter->uuids().contains(CLIENT_SDP_UUID, Qt::CaseInsensitive)) {
        BluezQt::PendingCall *callCP = btManager->registerProfile(clientProfile);
        callCP->waitForFinished();

        if (callCP->error()) {
            LOG_WARNING ("[Srvr]Error registering client profile" << callCP->errorText());
            return false;
        }
    }
    QObject::connect(clientProfile, &SdpProfile::incomingBTConnection,
            this, &BTConnection::handleIncomingBTConnection);
    QObject::connect(clientProfile, &SdpProfile::disconnectRequest,
            this, &BTConnection::handleDisconnectRequest);
    LOG_DEBUG("[Srvr]Client profile registered");

    QByteArray serverSDP;
    if (!readSRFromFile (SERVER_BT_SR_FILE, serverSDP)) {
        serverSDP = serverServiceRecordDef ().toLatin1 ();
    }

    serverProfile = new SdpProfile(BluezQt::Profile::ServerRole,QString(serverSDP));

    if (!adapter->uuids().contains(SERVER_SDP_UUID, Qt::CaseInsensitive)) {
        BluezQt::PendingCall *callSP = btManager->registerProfile(serverProfile);
        callSP->waitForFinished();
        if (callSP->error()) {
            LOG_WARNING ("[Srvr]Error registering server profile" << callSP->errorText());
            return false;
        }
    }

    QObject::connect(serverProfile, &SdpProfile::incomingBTConnection,
            this, &BTConnection::handleIncomingBTConnection);
    QObject::connect(serverProfile, &SdpProfile::disconnectRequest,
            this, &BTConnection::handleDisconnectRequest);
    LOG_DEBUG("[Srvr]Server profile registered");

    return true;
}

bool
BTConnection::removeServiceRecords ()
{
    FUNCTION_CALL_TRACE;

    BluezQt::AdapterPtr adapter = btManager->adapters().first();

    if (clientProfile && adapter->uuids().contains(CLIENT_SDP_UUID, Qt::CaseInsensitive)) {
        BluezQt::PendingCall *c1 = btManager->unregisterProfile (clientProfile);
        c1->waitForFinished ();
        if (c1->error() != 0) {
            LOG_WARNING ("[Srvr]Unregister Client profile failed: " << c1->errorText());
        }
    }

    if (serverProfile && adapter->uuids().contains(SERVER_SDP_UUID, Qt::CaseInsensitive)) {
        BluezQt::PendingCall *c2 = btManager->unregisterProfile (serverProfile);
        c2->waitForFinished ();
        if (c2->error() != 0) {
            LOG_WARNING ("[Srvr]Unregister Server profile failed: " << c2->errorText());
        }
    }

    return true;
}


bool
BTConnection::readSRFromFile (const QString filename, QByteArray &record)
{
    FUNCTION_CALL_TRACE;

    QDir dir(BTSRS_PATH);
    QFile srFile (dir.filePath(filename));
    if (!srFile.open (QIODevice::ReadOnly)) {
        LOG_WARNING ("Unable to open service record files");
        return false;
    }

    record = srFile.readAll ();

    srFile.close ();
    return true;
}

const QString
BTConnection::clientServiceRecordDef () const
{
    FUNCTION_CALL_TRACE
    return
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>                        \
<!-- As per the SyncML OBEX Binding for BT specification at         \
     http://technical.openmobilealliance.org/Technical/release_program/docs/Common/V1_2_1-20070813-A/OMA-TS-SyncML_OBEXBinding-V1_2-20070221-A.pdf  \
-->                                                                 \
<record>                                                            \
  <attribute id=\"0x0001\">                                         \
    <sequence>                                                      \
      <uuid value=\"00000002-0000-1000-8000-0002ee000002\" />       \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0004\">                                         \
    <sequence>                                                      \
      <sequence>                                                    \
        <uuid value=\"0x0100\" />                                   \
      </sequence>                                                   \
      <sequence>                                                    \
        <uuid value=\"0x0003\" />                                   \
        <uint8 value=\"25\" />                                      \
      </sequence>                                                   \
      <sequence>                                                    \
        <uuid value=\"0x0008\" />                                   \
      </sequence>                                                   \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0005\">                                         \
    <sequence>                                                      \
      <uuid value=\"0x1002\" />                                     \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0009\">                                         \
    <sequence>                                                      \
      <sequence>                                                    \
        <uuid value=\"00000002-0000-1000-8000-0002ee000002\" />     \
        <uint16 value=\"0x0100\" />                                 \
      </sequence>                                                   \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0100\">                                         \
    <text value=\"SyncML Client\" />                                \
  </attribute>                                                      \
</record>";
}

const QString
BTConnection::serverServiceRecordDef () const
{
    FUNCTION_CALL_TRACE
    return
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>                        \
<!-- As per the SyncML OBEX Binding for BT specification at         \
     http://technical.openmobilealliance.org/Technical/release_prog	ram/docs/Common/V1_2_1-20070813-A/OMA-TS-SyncML_OBEXBinding-V1_2-20070221-A.pdf	\
-->                                                                 \
<record>                                                            \
  <attribute id=\"0x0001\">                                         \
    <sequence>                                                      \
      <uuid value=\"00000001-0000-1000-8000-0002ee000001\" />       \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0004\">                                         \
    <sequence>                                                      \
      <sequence>                                                    \
        <uuid value=\"0x0100\" />                                   \
      </sequence>                                                   \
      <sequence>                                                    \
        <uuid value=\"0x0003\" />                                   \
        <uint8 value=\"26\" /> <!-- A fixed channel number -->      \
      </sequence>                                                   \
      <sequence>                                                    \
        <uuid value=\"0x0008\" />                                   \
      </sequence>                                                   \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0005\">                                         \
    <sequence>                                                      \
      <uuid value=\"0x1002\" />                                     \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0009\">                                         \
    <sequence>                                                      \
      <sequence>                                                    \
        <uuid value=\"00000001-0000-1000-8000-0002ee000001\" />     \
        <uint16 value=\"0x0100\" />                                 \
      </sequence>                                                   \
    </sequence>                                                     \
  </attribute>                                                      \
  <attribute id=\"0x0100\">                                         \
    <text value=\"SyncML Server\" />                                \
  </attribute>                                                      \
</record>";
}
