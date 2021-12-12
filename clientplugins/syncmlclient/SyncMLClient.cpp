/*
 * This file is part of buteo-sync-plugins package
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Sateesh Kavuri <sateesh.kavuri@nokia.com>
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
 *
 */

#include "SyncMLClient.h"

#include <QLibrary>
#include <QtNetwork>

#include <buteosyncfw5/PluginCbInterface.h>
#include <buteosyncml5/SyncAgent.h>
#include <buteosyncml5/SyncAgentConfig.h>
#include <buteosyncml5/SyncAgentConfigProperties.h>
#include <buteosyncml5/HTTPTransport.h>
#include <buteosyncml5/OBEXTransport.h>
#include <buteosyncml5/DeviceInfo.h>
#include "SyncMLPluginLogging.h"
#include <buteosyncfw5/ProfileEngineDefs.h>

#include <Accounts/Account>
#include "SyncMLConfig.h"
#include "SyncMLCommon.h"
#include "DeviceInfo.h"

const QString DEFAULTCONFIGFILE("/etc/buteo/meego-syncml-conf.xml");
const QString EXTCONFIGFILE("/etc/buteo/ext-syncml-conf.xml");


Buteo::ClientPlugin* SyncMLClientLoader::createClientPlugin(
        const QString& pluginName,
        const Buteo::SyncProfile& profile,
        Buteo::PluginCbInterface* cbInterface)
{
    return new SyncMLClient(pluginName, profile, cbInterface);
}


SyncMLClient::SyncMLClient(const QString& aPluginName,
		const Buteo::SyncProfile& aProfile,
		Buteo::PluginCbInterface *aCbInterface) :
	ClientPlugin(aPluginName, aProfile, aCbInterface), iAgent(0),
	iTransport(0), iConfig(0), iCommittedItems(0) {
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

SyncMLClient::~SyncMLClient() {
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    
}

bool SyncMLClient::init() {
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	iProperties = iProfile.allNonStorageKeys();

    if (initAgent() && initTransport() && initConfig()) {
        if (useAccounts () && initAccount()) {
            // Fetch the credentials from SSO. Currently, only "password"
            // method and mechanism are supported to be retrieved
            getCredentials();

            // Fetch the key/values settings from Account and merge them with iProperties
            QMap<QString, QString> accSettings = accountSettings();
            
            QMap<QString, QString>::iterator iter;
            for (iter = accSettings.begin(); iter != accSettings.end(); ++iter) {
                iProperties[iter.key()] = iter.value();
            }
        }
		return true;
	} else {
		// Uninitialize everything that was initialized before failure.
		uninit();

		return false;
	}

}

bool SyncMLClient::uninit()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	closeAgent();

	closeConfig();

	closeTransport();

	return true;
}

bool SyncMLClient::startSync()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if (iAgent == 0 || iConfig == 0 || iTransport == 0)
    {
		return false;
    }

    connect(iAgent, SIGNAL(stateChanged(DataSync::SyncState)),
            this, SLOT(syncStateChanged(DataSync::SyncState)));

    connect(iAgent, SIGNAL(syncFinished(DataSync::SyncState)),
            this, SLOT(syncFinished(DataSync::SyncState)));

	connect(iAgent, SIGNAL(itemProcessed(DataSync::ModificationType,
            DataSync::ModifiedDatabase, QString, QString,int)),
            this, SLOT(receiveItemProcessed(DataSync::ModificationType,
            DataSync::ModifiedDatabase, QString, QString,int)));

    connect(iAgent, SIGNAL(storageAccquired(QString)),
            this, SLOT(storageAccquired(QString)));

	iConfig->setTransport(iTransport);

    if (useAccounts()) // The actual sync start would be done in credentialsResponse() slot
        return true;
    else
        return iAgent->startSync(*iConfig);
}

void SyncMLClient::abortSync(Sync::SyncStatus aStatus)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    DataSync::SyncState state = DataSync::ABORTED;

    if (aStatus == Sync::SYNC_ERROR) {
        state = DataSync::CONNECTION_ERROR;
    }

    if( iAgent )
    {
        if( !iAgent->abort(state) )
        {
            qCDebug(lcSyncMLPlugin) << "Agent not active, aborting immediately";
            syncFinished(DataSync::ABORTED);

        }
        else
        {
            qCDebug(lcSyncMLPlugin) << "Agent active, abort event posted" ;
        }
    }
    else
    {
        qCWarning(lcSyncMLPlugin) << "abortSync() called before init(), ignoring";
    }

}

bool SyncMLClient::cleanUp() {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	iProperties = iProfile.allNonStorageKeys();
	initAgent();
	initConfig();

	bool retVal = iAgent->cleanUp(iConfig);

	closeAgent();
	closeConfig();
	return retVal;
}

void SyncMLClient::syncStateChanged(DataSync::SyncState aState) {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	switch(aState) {
	case DataSync::LOCAL_INIT:
	case DataSync::REMOTE_INIT: {
		emit syncProgressDetail(getProfileName(),Sync::SYNC_PROGRESS_INITIALISING);
		break;
	}
	case DataSync::SENDING_ITEMS: {
		emit syncProgressDetail(getProfileName(),Sync::SYNC_PROGRESS_SENDING_ITEMS);
		break;
	}
	case DataSync::RECEIVING_ITEMS: {
		emit syncProgressDetail(getProfileName(),Sync::SYNC_PROGRESS_RECEIVING_ITEMS);
		break;
	}
	case DataSync::FINALIZING: {
		emit syncProgressDetail(getProfileName(),Sync::SYNC_PROGRESS_FINALISING);
		break;
	}
	default:
		//do nothing
		break;
	};

#ifndef QT_NO_DEBUG
    qCDebug(lcSyncMLPlugin) << "***********  Sync Status has Changed to:" << toText(aState)
                    << "****************";
#endif  //  QT_NO_DEBUG
}

void SyncMLClient::syncFinished(DataSync::SyncState aState) {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

#ifndef QT_NO_DEBUG
    qCDebug(lcSyncMLPlugin) << "***********  Sync has finished with:" << toText(aState)
                    << "****************";
#endif  //  QT_NO_DEBUG
    switch(aState)
    {
        case DataSync::INTERNAL_ERROR:
        case DataSync::AUTHENTICATION_FAILURE:
        case DataSync::DATABASE_FAILURE:
        case DataSync::CONNECTION_ERROR:
        case DataSync::INVALID_SYNCML_MESSAGE:
        case DataSync::UNSUPPORTED_SYNC_TYPE:
        case DataSync::UNSUPPORTED_STORAGE_TYPE:
        {
            generateResults( false );
            emit error(getProfileName(), "", Buteo::SyncResults::ABORTED);
            break;
        }
        case DataSync::SUSPENDED:
        case DataSync::ABORTED:
        case DataSync::SYNC_FINISHED:
        {
            generateResults( true );
            emit success( getProfileName(), QString::number(aState));
            break;
        }
        case DataSync::NOT_PREPARED:
        case DataSync::PREPARED:
        case DataSync::LOCAL_INIT:
        case DataSync::REMOTE_INIT:
        case DataSync::SENDING_ITEMS:
        case DataSync::RECEIVING_ITEMS:
        case DataSync::FINALIZING:
        case DataSync::SUSPENDING:
        default:
        {
            // do nothing
            // @todo: do nothing??? We'll deadlock then! Fix this at some point!
            break;
        }
    }
}

void SyncMLClient::storageAccquired(QString aMimeType) {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    qCDebug(lcSyncMLPlugin) << " MimeType " << aMimeType;
	emit accquiredStorage(aMimeType);
}

void SyncMLClient::receiveItemProcessed(
        DataSync::ModificationType aModificationType,
        DataSync::ModifiedDatabase aModifiedDatabase, QString aLocalDatabase,
        QString aMimeType, int aCommittedItems) {

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Modification Type " << aModificationType;
    qCDebug(lcSyncMLPlugin) << "Modification Database " << aModifiedDatabase;
    qCDebug(lcSyncMLPlugin) << " Database " << aLocalDatabase;
    qCDebug(lcSyncMLPlugin) << " MimeType " << aMimeType;

    ++iCommittedItems;
    if(!receivedItems.contains(aLocalDatabase))
    {
        ReceivedItemDetails details;
        details.added = details.modified = details.deleted = details.error = 0;
        details.mime = aMimeType;
        receivedItems[aLocalDatabase] = details;
    }

    Sync::TransferDatabase db = Sync::LOCAL_DATABASE;

    switch (aModificationType) {
    case DataSync::MOD_ITEM_ADDED: {
        ++receivedItems[aLocalDatabase].added;
        break;
    }
    case DataSync::MOD_ITEM_MODIFIED: {
        ++receivedItems[aLocalDatabase].modified;
        break;
    }
    case DataSync::MOD_ITEM_DELETED: {
        ++receivedItems[aLocalDatabase].deleted;
        break;
    }
    case DataSync::MOD_ITEM_ERROR: {
        ++receivedItems[aLocalDatabase].error;
        break;
    }
    default: {
        Q_ASSERT(0);
        break;

    }

    }

    if (aModifiedDatabase == DataSync::MOD_LOCAL_DATABASE) {
        db = Sync::LOCAL_DATABASE;
    } else {
        db = Sync::REMOTE_DATABASE;
    }

    if( iCommittedItems == aCommittedItems )
    {
        QMapIterator<QString,ReceivedItemDetails> itr(receivedItems);
        while( itr.hasNext() )
        {
            itr.next();
            if( itr.value().added )
            {
                emit transferProgress(getProfileName(), db, Sync::ITEM_ADDED, itr.value().mime, itr.value().added);
            }
            if( itr.value().modified )
            {
                emit transferProgress(getProfileName(), db, Sync::ITEM_MODIFIED, itr.value().mime, itr.value().modified);
            }
            if( itr.value().deleted )
            {
                emit transferProgress(getProfileName(), db, Sync::ITEM_DELETED, itr.value().mime, itr.value().deleted);
            }
            if( itr.value().error )
            {
                emit transferProgress(getProfileName(), db, Sync::ITEM_ERROR, itr.value().mime, itr.value().error);
            }
        }
        iCommittedItems = 0;
        receivedItems.clear();
    }

}

bool SyncMLClient::initAgent() {

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    qCDebug(lcSyncMLPlugin) << "Creating agent...";

    bool success = false;

    iAgent = new DataSync::SyncAgent();
    if (!iAgent) {
        qCDebug(lcSyncMLPlugin) << "Agent creation failed";
    }
    else {
        success = true;
        qCDebug(lcSyncMLPlugin) << "Agent created";
    }
    return success;
}

void SyncMLClient::closeAgent() {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Destroying agent...";

	if (iAgent) {
            delete iAgent;
            iAgent = 0;
	}

}

bool SyncMLClient::initTransport() {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Initiating transport...";

	bool success = false;
	QString transportType = iProperties[PROF_SYNC_TRANSPORT];

	if (transportType == HTTP_TRANSPORT) {
		success = initHttpTransport();
	} else if (transportType == OBEX_TRANSPORT) {
		success = initObexTransport();
	} else {
	    qCDebug(lcSyncMLPlugin) << "Unknown transport type:" << transportType;
	}

	return success;
}

void SyncMLClient::closeTransport() {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Closing transport...";

    delete iTransport;
    iTransport = NULL;

    qCDebug(lcSyncMLPlugin) << "Transport closed";
}

bool SyncMLClient::initConfig() {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Initiating config...";

	QStringList storageNames = iProfile.subProfileNames(
			Buteo::Profile::TYPE_STORAGE);

	if (storageNames.isEmpty()) {
		qCCritical(lcSyncMLPlugin) << "No storages defined for profile, nothing to sync";
		return false;
	}

	if (!iStorageProvider.init(&iProfile, this, iCbInterface, false)) {
		qCCritical(lcSyncMLPlugin) << "Could not initialize storage provider";
		return false;
	}

	iConfig = new DataSync::SyncAgentConfig;

    // ** Read configuration

    // Two configuration files are being read: first the Meego default config file,
    // and then possible external config file, which can be used to add additional
    // configuration, or replace some of the configuration of Meego default config.

    // Default configuration file should always exist
    if( !iConfig->fromFile( DEFAULTCONFIGFILE ) )
    {
        qCCritical(lcSyncMLPlugin) << "Could not read default SyncML configuration file:" << DEFAULTCONFIGFILE;
        return false;
    }

    if( iConfig->fromFile( EXTCONFIGFILE ) )
    {
        qCDebug(lcSyncMLPlugin) << "Found & read external configuration file:" << EXTCONFIGFILE;
    }
    else
    {
        qCDebug(lcSyncMLPlugin) << "Could not find external configuration file" << EXTCONFIGFILE <<", skipping";
    }

	// ** Set up storage provider

	iConfig->setStorageProvider(&iStorageProvider);

	// ** Set up device info

	QString DEV_INFO_FILE_PATH = SyncMLConfig::getDevInfoFile();
	QFile devInfoFile(DEV_INFO_FILE_PATH);

	if (!devInfoFile.exists()) {
		Buteo::DeviceInfo appDevInfo;
		QMap < QString, QString > deviceInfoMap
				= appDevInfo.getDeviceInformation();
		appDevInfo.saveDevInfoToFile(deviceInfoMap, DEV_INFO_FILE_PATH);
	}

	DataSync::DeviceInfo syncDeviceInfo;
	syncDeviceInfo.readFromFile(DEV_INFO_FILE_PATH);
	iConfig->setDeviceInfo(syncDeviceInfo);

	// ** Set up sync targets

	for (int i = 0; i < storageNames.count(); ++i) {
		const Buteo::Profile *storageProfile = iProfile.subProfile(
				storageNames[i], Buteo::Profile::TYPE_STORAGE);

		QString sourceDb = storageProfile->key(STORAGE_SOURCE_URI);

		if (storageProfile->isEnabled()) {
			QString targetDb = storageProfile->key(STORAGE_REMOTE_URI);
		    qCDebug(lcSyncMLPlugin) << "Adding sync target:" << sourceDb << "->" << targetDb;
			iConfig->addSyncTarget(sourceDb, targetDb);
		} else {
		    qCDebug(lcSyncMLPlugin) << "Adding disabled sync target:" << sourceDb;
			iConfig->addDisabledSyncTarget(sourceDb);
		}

	}

	// ** Set up sync parameters
	QString transportType = iProperties[PROF_SYNC_TRANSPORT];

	QString remoteDeviceName;

	if (transportType == HTTP_TRANSPORT) {
		// Ovi.com requires remote device name to be the sync URI
		remoteDeviceName = iProperties[PROF_REMOTE_URI];
	} else if (transportType == OBEX_TRANSPORT) {
		// Over OBEX, set remote device to it's address as designated in profile
		remoteDeviceName = iProperties[PROF_REMOTE_ADDRESS];
		if (remoteDeviceName.isEmpty()) {
			// There is no code to set PROF_REMOTE_ADDRESS.
			// It may be set via Buteo::KEY_REMOTE_ID as it just happens to be the same.
			// Alternatively, it may be hardcoded into the profile from the template.
			// For Bluetooth OBEX however, it is sometimes never set.
			// Instead, use the KEY_REMOTE_NAME which will be set for OBEX profiles...
			remoteDeviceName = iProperties[Buteo::KEY_REMOTE_NAME];
		}
	}

	QString versionProp = iProperties[PROF_SYNC_PROTOCOL];
    DataSync::ProtocolVersion version = DataSync::SYNCML_1_2;

	if (versionProp == SYNCML11) {
	    qCDebug(lcSyncMLPlugin) << "Using SyncML DS 1.1 protocol";
        version = DataSync::SYNCML_1_1;
	} else if (versionProp == SYNCML12) {
	    qCDebug(lcSyncMLPlugin) << "Using SyncML DS 1.2 protocol";
        version = DataSync::SYNCML_1_2;
	}

	DataSync::SyncInitiator initiator = DataSync::INIT_CLIENT;

	if (transportType == HTTP_TRANSPORT) {
		initiator = DataSync::INIT_CLIENT;
	} else if (transportType == OBEX_TRANSPORT) {
		initiator = DataSync::INIT_SERVER;
	}

	DataSync::SyncDirection direction = resolveSyncDirection(initiator);
	bool forceSlowSync = iProfile.boolKey(Buteo::KEY_FORCE_SLOW_SYNC);

	DataSync::SyncMode syncMode(direction, initiator);
	if (forceSlowSync) {
		syncMode.toSlowSync();
	}

	iConfig->setSyncParams(remoteDeviceName, version, syncMode);

	// ** Set up auth parameters

    DataSync::AuthType type = DataSync::AUTH_NONE;
	QString username;
	QString password;

	if (transportType == HTTP_TRANSPORT) {
		type = DataSync::AUTH_BASIC;
		username = iProperties[PROF_USERID];
		password = iProperties[PROF_PASSWD];
	} else if (transportType == OBEX_TRANSPORT) {
		type = DataSync::AUTH_NONE;
	}

	iConfig->setAuthParams(type, username, password);

	// ** Set up other parameters

	DataSync::ConflictResolutionPolicy policy =
			resolveConflictResolutionPolicy(initiator);
	iConfig->setAgentProperty(DataSync::CONFLICTRESOLUTIONPOLICYPROP,
			QString::number(policy));

	if (transportType == HTTP_TRANSPORT) {
		// Make sure that S60 EMI tags are not sent over HTTP.
		iConfig->clearExtension(DataSync::EMITAGSEXTENSION);
	}

	return true;

}

void SyncMLClient::closeConfig() {

	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Closing config...";

	delete iConfig;
	iConfig = NULL;

	if (!iStorageProvider.uninit()) {
        qCCritical(lcSyncMLPlugin) << "Could not uninitialize storage provider";
	}

    qCDebug(lcSyncMLPlugin) << "Config closed";

}

Buteo::SyncResults SyncMLClient::getSyncResults() const
{
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	return iResults;
}

void SyncMLClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState)
{
    FUNCTION_CALL_TRACE (lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Received connectivity change event:"
            << aType << " changed to " << aState;
}

#ifndef QT_NO_DEBUG

// this function exists only debugging purposes.
// should not be used to send messages as feedback
// only the state of the app will be sent now
// and UI has to map to a localisation string based on
// the state of the stack
QString SyncMLClient::toText(const DataSync::SyncState& aState) {

	switch (aState) {
	case DataSync::NOT_PREPARED:
		return "NOT PREPARED";

	case DataSync::LOCAL_INIT:
	case DataSync::REMOTE_INIT:
		return "INITIALIZING";

	case DataSync::SENDING_ITEMS:
		return "SENDING ITEMS";

	case DataSync::RECEIVING_ITEMS:
		return "RECEIVING_ITEMS";

	case DataSync::SENDING_MAPPINGS:
		return "SENDING MAPPINGS";

	case DataSync::RECEIVING_MAPPINGS:
		return "RECEIVING MAPPINGS";

	case DataSync::FINALIZING:
		return "FINALIZING";

	case DataSync::SUSPENDING:
		return "SUSPENDING";

	case DataSync::PREPARED:
		return "PREPARED";

	case DataSync::SYNC_FINISHED:
		return "SYNC FINISHED";

	case DataSync::INTERNAL_ERROR:
		return "INTERNAL_ERROR";

	case DataSync::AUTHENTICATION_FAILURE:
		return "AUTHENTICATION FAILURE";

	case DataSync::DATABASE_FAILURE:
		return "DATABASE_FAILURE";

	case DataSync::SUSPENDED:
		return "SUSPENDED";

	case DataSync::ABORTED:
		return "ABORTED";

	case DataSync::CONNECTION_ERROR:
		return "CONNECTION ERROR";

	case DataSync::INVALID_SYNCML_MESSAGE:
		return "INVALID SYNCML MESSAGE";

    case DataSync::UNSUPPORTED_SYNC_TYPE:
        return "UNSUPPORTED SYNC TYPE";

    case DataSync::UNSUPPORTED_STORAGE_TYPE:
        return "UNSUPPORTED STORAGE TYPE";

	default:
		return "UNKNOWN";
		break;
	}

}
#endif //#ifndef QT_NO_DEBUG

bool SyncMLClient::initObexTransport()
{
    FUNCTION_CALL_TRACE (lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Creating OBEX transport";

    QString btAddress = iProperties[PROF_BT_ADDRESS];

    if (btAddress.isEmpty())
    {
        qCCritical(lcSyncMLPlugin) << "Could not find mandatory property:" << PROF_BT_ADDRESS;
        return false;
    }

    QString btService = iProperties[PROF_BT_UUID];

    if (btService.isEmpty())
    {
        qCCritical(lcSyncMLPlugin) << "Could not find mandatory property:" << PROF_BT_UUID;
        return false;
    }

    qCDebug(lcSyncMLPlugin) << "Using BT address:" << btAddress;
    qCDebug(lcSyncMLPlugin) << "Using BT service UUID:" << btService;

    iBTConnection.setConnectionInfo(btAddress, btService);

    DataSync::OBEXTransport* transport = new DataSync::OBEXTransport(iBTConnection, DataSync::OBEXTransport::MODE_OBEX_CLIENT, DataSync::OBEXTransport::TYPEHINT_BT);

    if (iProperties[PROF_USE_WBXML] == PROPS_TRUE)
    {
        qCDebug(lcSyncMLPlugin) << "Using wbXML";
        transport->setWbXml(true);
    }
    else
    {
        qCDebug(lcSyncMLPlugin) << "Not using wbXML";
        transport->setWbXml(false);
    }

    iTransport = transport;

    return true;

}

bool SyncMLClient::initHttpTransport() {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) << "Creating HTTP transport";

	QString remoteURI = iProperties[PROF_REMOTE_URI];
	bool success = false;

	if (!remoteURI.isEmpty()) {

		DataSync::HTTPTransport* transport = new DataSync::HTTPTransport();

	    qCDebug(lcSyncMLPlugin) << "Setting remote URI to" << remoteURI;
		transport->setRemoteLocURI(remoteURI);

		QString proxyHost = iProperties[PROF_HTTP_PROXY_HOST];
		if (!proxyHost.isEmpty()) {

			QString proxyPort = iProperties[PROF_HTTP_PROXY_PORT];

			QNetworkProxy proxy = transport->getProxyConfig();
			proxy.setType(QNetworkProxy::HttpProxy);
			proxy.setHostName(proxyHost);
			proxy.setPort(proxyPort.toInt());
			transport->setProxyConfig(proxy);

		    qCDebug(lcSyncMLPlugin) << "Using proxy";
		    qCDebug(lcSyncMLPlugin) << "   host: " << proxyHost;
		    qCDebug(lcSyncMLPlugin) << "   port: " << proxyPort;
		} else {
		    qCDebug(lcSyncMLPlugin) << "Not using proxy";
		}

		if (iProperties[PROF_USE_WBXML] == PROPS_TRUE) {
		    qCDebug(lcSyncMLPlugin) << "Using wbXML";
			transport->setWbXml(true);
		} else {
		    qCDebug(lcSyncMLPlugin) << "Not using wbXML";
			transport->setWbXml(false);
		}

		QString xheaders = iProperties[PROF_HTTP_XHEADERS];
		QStringList hdrlist = xheaders.split("\r\n");
		foreach (QString hdr, hdrlist) {
			QString fname = hdr.section(':', 0, 0);
			QString fvalue = hdr.section(':', 1);
		    qCDebug(lcSyncMLPlugin) << "fname: " << fname << ", fvalue" << fvalue;
			transport->addXheader(fname, fvalue);
		}

		iTransport = transport;
		success = true;
	} else {
	    qCDebug(lcSyncMLPlugin) << "Could not find 'Remote database' property";
	}

	return success;
}

DataSync::SyncDirection SyncMLClient::resolveSyncDirection(
		const DataSync::SyncInitiator& aInitiator) {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	Buteo::SyncProfile::SyncDirection directionFromProfile =
			iProfile.syncDirection();

	DataSync::SyncDirection direction = DataSync::DIRECTION_TWO_WAY;

	if (aInitiator == DataSync::INIT_CLIENT) {

		if (directionFromProfile
				== Buteo::SyncProfile::SYNC_DIRECTION_FROM_REMOTE) {
			direction = DataSync::DIRECTION_FROM_SERVER;
		} else if (directionFromProfile
				== Buteo::SyncProfile::SYNC_DIRECTION_TO_REMOTE) {
			direction = DataSync::DIRECTION_FROM_CLIENT;
		}
	} else if (aInitiator == DataSync::INIT_SERVER) {
		if (directionFromProfile
				== Buteo::SyncProfile::SYNC_DIRECTION_FROM_REMOTE) {
			direction = DataSync::DIRECTION_FROM_CLIENT;
		} else if (directionFromProfile
				== Buteo::SyncProfile::SYNC_DIRECTION_TO_REMOTE) {
			direction = DataSync::DIRECTION_FROM_SERVER;
		}
	}

	return direction;
}

DataSync::ConflictResolutionPolicy SyncMLClient::resolveConflictResolutionPolicy(
		const DataSync::SyncInitiator& aInitiator) {
	FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

	Buteo::SyncProfile::ConflictResolutionPolicy crPolicyFromProfile =
			iProfile.conflictResolutionPolicy();

	/* In case if we have to resolve conflict the choice will be based on the user selection when
	 * creating a sync profile , if to prefer local changes or remote changes.
	 */
	DataSync::ConflictResolutionPolicy crPolicy =
			DataSync::PREFER_LOCAL_CHANGES;

	switch (crPolicyFromProfile) {
	case Buteo::SyncProfile::CR_POLICY_PREFER_LOCAL_CHANGES: {
	    qCDebug(lcSyncMLPlugin) << "Buteo::SyncProfile::CR_POLICY_PREFER_LOCAL_CHANGES";
		crPolicy = DataSync::PREFER_LOCAL_CHANGES;
		break;
	}

	case Buteo::SyncProfile::CR_POLICY_PREFER_REMOTE_CHANGES: {
	    qCDebug(lcSyncMLPlugin) << "Buteo::SyncProfile::CR_POLICY_PREFER_REMOTE_CHANGES";
		crPolicy = DataSync::PREFER_REMOTE_CHANGES;
		break;
	}

	default: {
		break;
	}
	}

	return crPolicy;
}

void SyncMLClient::generateResults( bool aSuccessful )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    iResults.setMajorCode( aSuccessful ? Buteo::SyncResults::SYNC_RESULT_SUCCESS : Buteo::SyncResults::SYNC_RESULT_FAILED );

    iResults.setTargetId(iAgent->getResults().getRemoteDeviceId());
    const QMap<QString, DataSync::DatabaseResults>* dbResults = iAgent->getResults().getDatabaseResults();

    if (dbResults->isEmpty())
    {
        qCDebug(lcSyncMLPlugin) << "No items transferred";
    }
    else
    {
        QMapIterator<QString, DataSync::DatabaseResults> i( *dbResults );
        while ( i.hasNext() )
        {
            i.next();
            const DataSync::DatabaseResults& r = i.value();
            Buteo::TargetResults targetResults(
                    i.key(), // Target name
                    Buteo::ItemCounts( r.iLocalItemsAdded,
                                       r.iLocalItemsDeleted,
                                       r.iLocalItemsModified ),
                    Buteo::ItemCounts( r.iRemoteItemsAdded,
                                       r.iRemoteItemsDeleted,
                                       r.iRemoteItemsModified ));
            iResults.addTargetResults( targetResults );

            qCDebug(lcSyncMLPlugin) << "Items for" << targetResults.targetName() << ":";
            qCDebug(lcSyncMLPlugin) << "LA:" << targetResults.localItems().added <<
                    "LD:" << targetResults.localItems().deleted <<
                    "LM:" << targetResults.localItems().modified <<
                    "RA:" << targetResults.remoteItems().added <<
                    "RD:" << targetResults.remoteItems().deleted <<
                    "RM:" << targetResults.remoteItems().modified;
        }
    }
}

Accounts::AccountId SyncMLClient::accountId()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Accounts::AccountId accountId = 0;
    QStringList accountList = iProfile.keyValues ( Buteo::KEY_ACCOUNT_ID );
    if ( !accountList.isEmpty() )
    {
        accountId = accountList.first().toUInt();
    }
    
    return accountId;
}

bool SyncMLClient::initAccount()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    Accounts::Manager* manager = new Accounts::Manager();

    Accounts::AccountId accId = accountId();

    if ( accId != (Accounts::AccountId)0 ) {
        iAccount = manager->account( accId );
        return true;
    } else {
        return false;
    }
}

void SyncMLClient::getCredentials ()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    quint32 credentialsId = iAccount->credentialsId();

    SignOn::Identity* identity = SignOn::Identity::existingIdentity( credentialsId );

    SignOn::SessionData data;

    // Currently, we support only "password" method and mechanism for
    // SyncML
    iAuthSession = identity->createSession( QLatin1String("password") );
    QObject::connect( iAuthSession, SIGNAL(response(const SignOn::SessionData &)),
                      this, SLOT(credentialsResponse(const SessionData&)));
    QObject::connect( iAuthSession, SIGNAL(error(const SignOn::Error &)),
        this, SLOT(credentialsError(const Error&)));

    iAuthSession->process(data, QLatin1String("password"));    
}

bool SyncMLClient::useAccounts() const
{
    return iProfile.boolKey( Buteo::KEY_USE_ACCOUNTS );
}

QMap<QString,QString> SyncMLClient::accountSettings() const
{
    QStringList keys = iAccount->allKeys();
    QMap<QString,QString> accSettings;
    foreach (const QString key, keys) {
        accSettings[key] = iAccount->valueAsString( key );
    }

    return accSettings;
}

void SyncMLClient::credentialsResponse( const SignOn::SessionData &sessionData )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QStringList sdpns = sessionData.propertyNames();
    foreach (const QString &sdpn, sdpns) {
        qCDebug(lcSyncMLPlugin) << sdpn << sessionData.getProperty(sdpn).toString();

        if (sdpn.compare("username", Qt::CaseInsensitive) == 0)
            iProperties[Buteo::KEY_USERNAME] = sessionData.getProperty( sdpn ).toString();
        else if (sdpn.compare("secret", Qt::CaseInsensitive) == 0)
            iProperties[Buteo::KEY_PASSWORD] = sessionData.getProperty( sdpn ).toString();
    }

    if ( iProperties[Buteo::KEY_USERNAME].isEmpty() ||
         iProperties[Buteo::KEY_PASSWORD].isEmpty() )
    {
        SignOn::Error error(SignOn::Error::Unknown, "Empty username or password returned from signond");
        credentialsError(error);
    }

    // Start the actual sync process
    if (iAgent)
    {
        // Set the config with the credentials from SSO
	    iConfig->setAuthParams(DataSync::AUTH_BASIC,
                               iProperties[Buteo::KEY_USERNAME],
                               iProperties[Buteo::KEY_PASSWORD]);
	    iAgent->startSync(*iConfig);
    }
}

void SyncMLClient::credentialsError( const SignOn::Error &error )
{
    qCWarning(lcSyncMLPlugin) << "Error in retrieving credentials from SSO."
            << error.type() << error.message();
    qCWarning(lcSyncMLPlugin) << "Emitting authentication failure";

    // Emitting authentication failure for lack of a proper enum from
    // DataSync::
    syncFinished(DataSync::AUTHENTICATION_FAILURE);
}
