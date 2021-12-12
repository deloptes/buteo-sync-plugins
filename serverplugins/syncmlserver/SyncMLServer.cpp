/*
* This file is part of buteo-sync-plugins package
*
* Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
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
#include "SyncMLServer.h"
#include "SyncMLPluginLogging.h"

#include <buteosyncfw5/SyncProfile.h>
#include <buteosyncml5/OBEXTransport.h>
#include <buteosyncml5/SyncAgentConfig.h>
#include <buteosyncfw5/PluginCbInterface.h>

#include "SyncMLConfig.h"
#include "DeviceInfo.h"

Buteo::ServerPlugin* SyncMLServerLoader::createServerPlugin(
        const QString& pluginName,
        const Buteo::Profile& profile,
        Buteo::PluginCbInterface* cbInterface)
{
    return new SyncMLServer(pluginName, profile, cbInterface);
}


SyncMLServer::SyncMLServer (const QString& pluginName,
                            const Buteo::Profile profile,
                            Buteo::PluginCbInterface *cbInterface) :
    ServerPlugin (pluginName, profile, cbInterface), mAgent (0), mConfig (0),
    mTransport (0), mCommittedItems (0), mConnectionType (Sync::CONNECTIVITY_USB),
    mIsSessionInProgress (false), mBTActive (false), mUSBActive (false)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

SyncMLServer::~SyncMLServer ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    closeSyncAgentConfig ();
    closeSyncAgent ();
    if (mUSBActive)
        closeUSBTransport ();
    if (mBTActive)
        closeBTTransport ();
    delete mTransport;
}

bool
SyncMLServer::init ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    return true;
}

bool
SyncMLServer::uninit ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    closeSyncAgentConfig ();
    closeSyncAgent ();

    // uninit() is called after completion of every sync session
    // Do not invoke close of transports, since in server mode
    // sync would be initiated from external entities and so
    // transport has to be open

    return true;
}

void
SyncMLServer::abortSync (Sync::SyncStatus status)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    DataSync::SyncState state = DataSync::ABORTED;

    if (status == Sync::SYNC_ERROR)
        state = DataSync::CONNECTION_ERROR;

    if (mAgent && mAgent->abort (state))
    {
        qCDebug(lcSyncMLPlugin) << "Signaling SyncML agent abort";
    } else
    {
        handleSyncFinished (DataSync::ABORTED);
    }
}

bool
SyncMLServer::cleanUp ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // FIXME: Perform necessary cleanup
    return true;
}

Buteo::SyncResults
SyncMLServer::getSyncResults () const
{
    return mResults;
}

bool
SyncMLServer::startListen ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Starting listener";

    bool listening = false;
    if (iCbInterface->isConnectivityAvailable (Sync::CONNECTIVITY_USB))
    {
        mUSBActive = listening = createUSBTransport ();
    }
    
    if (iCbInterface->isConnectivityAvailable (Sync::CONNECTIVITY_BT))
    {
        mBTActive = listening |= createBTTransport ();
    }
    
    if (iCbInterface->isConnectivityAvailable (Sync::CONNECTIVITY_INTERNET))
    {
        // No sync over IP as of now
    }

    return listening;
}

void
SyncMLServer::stopListen ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // Stop all connections
    if (mUSBActive)
        closeUSBTransport ();
    if (mBTActive)
        closeBTTransport ();
}

void
SyncMLServer::suspend ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // Not implementing suspend
}

void
SyncMLServer::resume ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // Not implementing suspend
}

void
SyncMLServer::connectivityStateChanged (Sync::ConnectivityType type, bool state)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Connectivity state changed event " << type << ". Connectivity changed to " << state;

    if (type == Sync::CONNECTIVITY_USB)
    {
        // Only connectivity changes would be USB enabled/disabled
        if (state)
        {
            qCDebug(lcSyncMLPlugin) << "USB available. Starting sync...";
            mUSBActive = createUSBTransport ();
        } else {
            qCDebug(lcSyncMLPlugin) << "USB connection not available. Stopping sync...";
            closeUSBTransport ();
            mUSBActive = false;

            // FIXME: Should we also abort any ongoing sync session?
        }
    } else if (type == Sync::CONNECTIVITY_BT)
    {
        if (state)
        {
            qCDebug(lcSyncMLPlugin) << "BT connection is available. Creating BT connection...";
            mBTActive = createBTTransport ();
        } else
        {
            qCDebug(lcSyncMLPlugin) << "BT connection unavailable. Closing BT connection...";
            closeBTTransport();
            mBTActive = false;
        }
    }
}

bool
SyncMLServer::initSyncAgent ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Creating SyncML agent...";

    mAgent = new DataSync::SyncAgent ();
    return true;
}

void
SyncMLServer::closeSyncAgent ()
{
    delete mAgent;
    mAgent = 0;
}

DataSync::SyncAgentConfig*
SyncMLServer::initSyncAgentConfig ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if (!mTransport || !mStorageProvider.init (&iProfile, this, iCbInterface, true))
        return 0;

    mConfig = new DataSync::SyncAgentConfig ();

    QString defaultSyncMLConfigFile, extSyncMLConfigFile;
    SyncMLConfig::syncmlConfigFilePaths (defaultSyncMLConfigFile, extSyncMLConfigFile);
    if (!mConfig->fromFile (defaultSyncMLConfigFile))
    {
        qCCritical(lcSyncMLPlugin) << "Unable to read default SyncML config";
        delete mConfig;
        mConfig = 0;
        return mConfig;
    }

    if (!mConfig->fromFile (extSyncMLConfigFile))
    {
        qCDebug(lcSyncMLPlugin) << "Could not find external configuration file";
    }

    mConfig->setStorageProvider (&mStorageProvider);
    mConfig->setTransport (mTransport);

    // Do we need to read the device info from file?
    QString DEV_INFO_FILE = SyncMLConfig::getDevInfoFile();
    QFile devInfoFile (DEV_INFO_FILE);

    if (!devInfoFile.exists ())
    {
        Buteo::DeviceInfo devInfo;
        QMap<QString,QString> deviceInfoMap = devInfo.getDeviceInformation ();
        devInfo.saveDevInfoToFile (deviceInfoMap, DEV_INFO_FILE);
    }

    DataSync::DeviceInfo syncDeviceInfo;
    syncDeviceInfo.readFromFile (DEV_INFO_FILE);
    mConfig->setDeviceInfo (syncDeviceInfo);

    return mConfig;
}

void
SyncMLServer::closeSyncAgentConfig ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Closing config...";

    delete mConfig;
    mConfig = 0;

    if (!mStorageProvider.uninit ())
        qCCritical(lcSyncMLPlugin) << "Unable to close storage provider";
}

bool
SyncMLServer::createUSBTransport ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Opening new USB connection";

    mUSBConnection.connect ();

    QObject::connect (&mUSBConnection, SIGNAL (usbConnected (int)),
                      this, SLOT (handleUSBConnected (int)));

    return mUSBConnection.isConnected ();
}

bool
SyncMLServer::createBTTransport ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    
    qCDebug(lcSyncMLPlugin) << "Creating new BT connection";
    bool btInitRes = mBTConnection.init ();
    
    QObject::connect (&mBTConnection, SIGNAL (btConnected (int, QString)),
                      this, SLOT (handleBTConnected (int, QString)));
    
    return btInitRes;
}

void
SyncMLServer::closeUSBTransport ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QObject::disconnect (&mUSBConnection, SIGNAL (usbConnected (int)),
                this, SLOT (handleUSBConnected (int)));
    mUSBConnection.disconnect ();
}

void
SyncMLServer::closeBTTransport ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    
    QObject::disconnect (&mBTConnection, SIGNAL (btConnected (int, QString)),
                         this, SLOT (handleBTConnected (int, QString)));
    mBTConnection.uninit ();
}

void
SyncMLServer::handleUSBConnected (int fd)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    Q_UNUSED (fd);

    if (mIsSessionInProgress)
    {
        qCDebug(lcSyncMLPlugin) << "Sync session is in progress over transport " << mConnectionType;
        emit sessionInProgress (mConnectionType);
        return;
    }

    qCDebug(lcSyncMLPlugin) << "New incoming data over USB";

    if (mTransport == NULL)
    {
        mTransport = new DataSync::OBEXTransport (mUSBConnection,
                                              DataSync::OBEXTransport::MODE_OBEX_SERVER,
                                              DataSync::OBEXTransport::TYPEHINT_USB);
    }
    
    if (!mTransport)
    {
        qCDebug(lcSyncMLPlugin) << "Creation of USB transport failed";
        return;
    }

    if (!mAgent)
    {
        mConnectionType = Sync::CONNECTIVITY_USB;
        startNewSession ("USB");
    }
}

void
SyncMLServer::handleBTConnected (int fd, QString btAddr)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    Q_UNUSED (fd);

    if (mIsSessionInProgress)
    {
        qCDebug(lcSyncMLPlugin) << "Sync session is in progress over transport " << mConnectionType;
        emit sessionInProgress (mConnectionType);
        return;
    }

    qCDebug(lcSyncMLPlugin) << "New incoming connection over BT";
    
    if (mTransport == NULL)
    {
        mTransport = new DataSync::OBEXTransport (mBTConnection,
                                              DataSync::OBEXTransport::MODE_OBEX_SERVER,
                                              DataSync::OBEXTransport::TYPEHINT_BT);
    }
    
    if (!mTransport)
    {
        qCDebug(lcSyncMLPlugin) << "Creation of BT transport failed";
        return;
    }
    
    if (!mAgent)
    {
        mConnectionType = Sync::CONNECTIVITY_BT;
        startNewSession (btAddr);
    }
}

bool
SyncMLServer::startNewSession (QString address)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if (!initSyncAgent () || !initSyncAgentConfig ())
        return false;

    QObject::connect (mAgent, SIGNAL (stateChanged (DataSync::SyncState)),
             this, SLOT (handleStateChanged (DataSync::SyncState)));
    QObject::connect (mAgent, SIGNAL (syncFinished (DataSync::SyncState)),
             this, SLOT (handleSyncFinished (DataSync::SyncState)));
    QObject::connect (mAgent, SIGNAL (storageAccquired (QString)),
             this, SLOT (handleStorageAccquired (QString)));
    QObject::connect (mAgent, SIGNAL (itemProcessed (DataSync::ModificationType, DataSync::ModifiedDatabase, QString, QString, int)),
             this, SLOT (handleItemProcessed (DataSync::ModificationType, DataSync::ModifiedDatabase, QString, QString, int)));

    mIsSessionInProgress = true;

    if (mAgent->listen (*mConfig))
    {
        emit newSession (address);
        return true;
    } else
    {
        return false;
    }
}

void
SyncMLServer::handleStateChanged (DataSync::SyncState state)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "SyncML new state " << state;
}

void
SyncMLServer::handleSyncFinished (DataSync::SyncState state)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Sync finished with state " << state;
    bool errorStatus = true;

    switch (state)
    {
    case DataSync::SUSPENDED:
    case DataSync::ABORTED:
    case DataSync::SYNC_FINISHED:
    {
        generateResults (true);
        errorStatus = false;
        emit success(getProfileName(), QString::number(state));
        break;
    }

    case DataSync::INTERNAL_ERROR:
    case DataSync::DATABASE_FAILURE:
    case DataSync::CONNECTION_ERROR:
    case DataSync::INVALID_SYNCML_MESSAGE:
    {
        generateResults (false);
        emit error(getProfileName(), QString::number(state), Buteo::SyncResults::INTERNAL_ERROR);
        break;
    }

    default:
    {
        qCCritical(lcSyncMLPlugin) << "Unexpected state change";
        generateResults (false);

        emit error(getProfileName(), QString::number(state), Buteo::SyncResults::INTERNAL_ERROR);
        break;
    }
    }

    uninit ();

    // Signal the USBConnection that sync has finished
    if (mConnectionType == Sync::CONNECTIVITY_USB)
        mUSBConnection.handleSyncFinished (errorStatus);
    else if (mConnectionType == Sync::CONNECTIVITY_BT)
        mBTConnection.handleSyncFinished (errorStatus);

    mIsSessionInProgress = false;
}

void
SyncMLServer::handleStorageAccquired (QString type)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    // emit signal that storage has been acquired
    emit accquiredStorage (type);
}

void
SyncMLServer::handleItemProcessed (DataSync::ModificationType modificationType,
                                   DataSync::ModifiedDatabase modifiedDb,
                                   QString localDb,
                                   QString dbType,
                                   int committedItems)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Modification type:" << modificationType;
    qCDebug(lcSyncMLPlugin) << "ModificationType database:" << modifiedDb;
    qCDebug(lcSyncMLPlugin) << "Local database:" << localDb;
    qCDebug(lcSyncMLPlugin) << "Database type:" << dbType;
    qCDebug(lcSyncMLPlugin) << "Committed items:" << committedItems;

    mCommittedItems++;

    if (!receivedItems.contains (localDb))
    {
        ReceivedItemDetails details;
        details.added = details.modified = details.deleted = details.error = 0;
        details.mime = dbType;
        receivedItems[localDb] = details;
    }

    switch (modificationType)
    {
    case DataSync::MOD_ITEM_ADDED:
    {
        ++receivedItems[localDb].added;
        break;
    }
    case DataSync::MOD_ITEM_MODIFIED:
    {
        ++receivedItems[localDb].modified;
        break;
    }
    case DataSync::MOD_ITEM_DELETED:
    {
        ++receivedItems[localDb].deleted;
        break;
    }
    case DataSync::MOD_ITEM_ERROR:
    {
        ++receivedItems[localDb].error;
        break;
    }
    default:
    {
        Q_ASSERT (0);
        break;
    }
    }

    Sync::TransferDatabase db = Sync::LOCAL_DATABASE;
    if (modifiedDb == DataSync::MOD_LOCAL_DATABASE)
        db = Sync::LOCAL_DATABASE;
    else
        db = Sync::REMOTE_DATABASE;

    if (mCommittedItems == committedItems)
    {
        QMapIterator<QString, ReceivedItemDetails> itr (receivedItems);
        while (itr.hasNext ())
        {
            itr.next ();
            if (itr.value ().added)
                emit transferProgress (getProfileName (), db, Sync::ITEM_ADDED, itr.value ().mime, itr.value ().added);
            if (itr.value ().modified)
                emit transferProgress (getProfileName (), db, Sync::ITEM_MODIFIED, itr.value ().mime, itr.value ().modified);
            if (itr.value ().deleted)
                emit transferProgress (getProfileName (), db, Sync::ITEM_DELETED, itr.value ().mime, itr.value ().deleted);
            if (itr.value ().error)
                emit transferProgress (getProfileName (), db, Sync::ITEM_ERROR, itr.value ().mime, itr.value ().error);
        }

        mCommittedItems = 0;
        receivedItems.clear ();
    }
}

void
SyncMLServer::generateResults (bool success)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    mResults.setMajorCode (success ? Buteo::SyncResults::SYNC_RESULT_SUCCESS : Buteo::SyncResults::SYNC_RESULT_FAILED);

    mResults.setTargetId (mAgent->getResults().getRemoteDeviceId ());
    const QMap<QString, DataSync::DatabaseResults>* dbResults = mAgent->getResults ().getDatabaseResults ();

    if (dbResults->isEmpty ())
    {
        qCDebug(lcSyncMLPlugin) << "No items transferred";
    }
    else
    {
        QMapIterator<QString, DataSync::DatabaseResults> itr (*dbResults);
        while (itr.hasNext ())
        {
            itr.next ();
            const DataSync::DatabaseResults& r = itr.value ();
            Buteo::TargetResults targetResults(
                    itr.key(), // Target name
                    Buteo::ItemCounts (r.iLocalItemsAdded,
                                       r.iLocalItemsDeleted,
                                       r.iLocalItemsModified),
                    Buteo::ItemCounts (r.iRemoteItemsAdded,
                                       r.iRemoteItemsDeleted,
                                       r.iRemoteItemsModified));
            mResults.addTargetResults (targetResults);

            qCDebug(lcSyncMLPlugin) << "Items for" << targetResults.targetName () << ":";
            qCDebug(lcSyncMLPlugin) << "LA:" << targetResults.localItems ().added <<
                      "LD:" << targetResults.localItems ().deleted <<
                      "LM:" << targetResults.localItems ().modified <<
                      "RA:" << targetResults.remoteItems ().added <<
                      "RD:" << targetResults.remoteItems ().deleted <<
                      "RM:" << targetResults.remoteItems ().modified;
        }
    }
}
