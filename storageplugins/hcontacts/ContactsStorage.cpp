/*
 * This file is part of buteo-sync-plugins package
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2013 - 2021 Jolla Ltd.
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
#include <QFile>
#include <QStringListIterator>
#include "SyncMLPluginLogging.h"
#include "ContactsStorage.h"
#include "SimpleItem.h"
#include "SyncMLCommon.h"
#include "SyncMLConfig.h"
#include "ProfileEngineDefs.h"

#include <QContact>

const char* CTCAPSFILENAME11 = "CTCaps_contacts_11.xml";
const char* CTCAPSFILENAME12 = "CTCaps_contacts_12.xml";


ContactStorage::ContactStorage(const QString& aPluginName)
 : Buteo::StoragePlugin(aPluginName), iBackend( 0 )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

ContactStorage::~ContactStorage()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    if(iBackend) {
        qCWarning(lcSyncMLPlugin) << "Uninit method has not been called!";
        delete iBackend;
        iBackend = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////////
////////////       Below functions are derived from storage plugin   ///////////////
////////////////////////////////////////////////////////////////////////////////////

bool ContactStorage::init( const QMap<QString, QString>& aProperties )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    iDeletedItems.uninit();

    const QString dbFile = "hcontacts.db";
    QString fullDbPath = SyncMLConfig::getDatabasePath() + dbFile;

    if( !iDeletedItems.init(fullDbPath) ) {
        return false;
    }

    QVersitDocument::VersitType vCardVersion;

    iProperties = aProperties;

    QString propVersion = iProperties[STORAGE_DEFAULT_MIME_VERSION_PROP];

    if(propVersion == "3.0") {
        qCDebug(lcSyncMLPlugin) << "Storage is using VCard version 3.0";
        vCardVersion =  QVersitDocument::VCard30Type;
        iProperties[STORAGE_DEFAULT_MIME_PROP] = "text/vcard";
    }
    else {
        qCDebug(lcSyncMLPlugin) << "Storage is using VCard version 2.1";
        vCardVersion = QVersitDocument::VCard21Type;
    }

    iProperties[STORAGE_SYNCML_CTCAPS_PROP_11] = getCtCaps( CTCAPSFILENAME11 );
    iProperties[STORAGE_SYNCML_CTCAPS_PROP_12] = getCtCaps( CTCAPSFILENAME12 );

    iBackend = new ContactsBackend(vCardVersion,
                                   iProperties.value(STORAGE_SYNC_TARGET),
                                   iProperties.value(STORAGE_ORIGIN_ID));

    if( !iBackend->init() ) {
        qCCritical(lcSyncMLPlugin) <<  "Failed to init contacts backend";
        return false;
    }

    if( !doInitItemAnalysis() ) {
        return false;
    }

    return true;
}

bool ContactStorage::uninit()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    doUninitItemAnalysis();

    // If the backend object is NULL, there is nothing to do anyway,
    // so the default value can be 'true' here.
    bool backendUninitOk = true;

    if( iBackend != NULL) {
        backendUninitOk = iBackend->uninit();
        delete iBackend;
        iBackend = NULL;
    }

    bool deleteItemsIdStorageUninitOk = iDeletedItems.uninit();

    return (backendUninitOk && deleteItemsIdStorageUninitOk);
}

bool ContactStorage::getAllItems(QList<Buteo::StorageItem*> &aItems)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        bool operationStatus = false;
        QList<QContactLocalId>  list;

        if(iBackend) {
                list = iBackend->getAllContactIds();
                if(list.size() != 0) {
                        qCDebug(lcSyncMLPlugin) << " Number of items retrieved from Contacts " << list.size();
                        aItems = getStoreList(list);
                }
                operationStatus = true;
        }
        return operationStatus;
}

bool ContactStorage::getAllItemIds( QList<QString>& aItems )
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        bool operationStatus = false;
        QList<QContactLocalId>  list;
        if(iBackend) {
                list = iBackend->getAllContactIds();
                qCDebug(lcSyncMLPlugin) << " Number of items retrieved from Contacts " << list.size();
                foreach(QContactLocalId id , list) {
                        aItems.append(id.toString());
                }
                operationStatus = true;
        }
        return operationStatus;
}

bool ContactStorage::getNewItems(QList<Buteo::StorageItem*> &aItems ,const QDateTime& aTime)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        bool operationStatus = false;
        QList<QContactLocalId>  list;
        if(iBackend) {
                qCDebug(lcSyncMLPlugin) << "****** getNewItems : Added After: ********" << aTime;
                list = iBackend->getAllNewContactIds(aTime);
                if(list.size() != 0) {
                        qCDebug(lcSyncMLPlugin) << "New Item List Size is " << list.size();
                        aItems = getStoreList(list);
                }
                operationStatus = true;
        }
        return operationStatus;
}

bool ContactStorage::getNewItemIds( QList<QString>& aNewItemIds, const QDateTime& aTime )
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        bool operationStatus = false;
        QList<QContactLocalId>  list;

        if(iBackend) {
                qCDebug(lcSyncMLPlugin) << "****** getNewItem Ids : Added After: ********" << aTime;
                list = iBackend->getAllNewContactIds(aTime);

                foreach(QContactLocalId id , list) {
                        aNewItemIds.append(id.toString());
                }

                operationStatus = true;
        }
        return operationStatus;
}

bool ContactStorage::getModifiedItems( QList<Buteo::StorageItem*>& aModifiedItems, const QDateTime& aTime )
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        QList<QContactLocalId>  list;
        bool operationStatus = false;

        if(iBackend) {
                qCDebug(lcSyncMLPlugin) << "******* getModifiedItems: From ********" << aTime;

                list = iBackend->getAllModifiedContactIds(aTime);

                aModifiedItems = getStoreList(list);

                operationStatus = true;
        }

        return operationStatus;
}

bool ContactStorage::getModifiedItemIds( QList<QString>& aModifiedItemIds, const QDateTime& aTime )
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        QList<QContactLocalId>  list;
        bool operationStatus = false;

        if(iBackend) {
                qCDebug(lcSyncMLPlugin) << "******* getModifiedItemIds : From ********" << aTime;

                list = iBackend->getAllModifiedContactIds(aTime);

                foreach(QContactLocalId id , list) {
                        aModifiedItemIds.append(id.toString());
                }
                operationStatus = true;
        }
        return operationStatus;
}

bool ContactStorage::getDeletedItemIds( QList<QString>& aDeletedItemIds, const QDateTime& aTime )
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    qCDebug(lcSyncMLPlugin) << "Getting deleted contacts since" << aTime;

    return iDeletedItems.getDeletedItems( aDeletedItemIds, aTime );
}

Buteo::StorageItem* ContactStorage::newItem()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    return new SimpleItem;
}

QList<Buteo::StorageItem*> ContactStorage::getItems( const QStringList& aItemIdList )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<Buteo::StorageItem*> items;
    QMap<QString,QString> vcards;
    QList<QContactLocalId> ids;
    QStringListIterator itr( aItemIdList );

    if( iBackend )
    {
        foreach (const QString itr, aItemIdList)
        {
            ids.append( QContactId::fromString (itr) );
        }
        iBackend->getContacts( ids, vcards );

        QMapIterator<QString,QString> i(vcards);
        while( i.hasNext() )
        {
            i.next();
            if( !i.value().isEmpty() )
            {
                SimpleItem *item = new SimpleItem;
                item->setId( i.key() );
                item->setType( iProperties[STORAGE_DEFAULT_MIME_PROP] );
                item->write( 0, i.value().toUtf8() );
                items.append( item );
            }
            else
            {
                qCWarning(lcSyncMLPlugin) << "Contact with id " << i.key() <<" doesn't exist!";
            }
        }
    }

    return items;
}

Buteo::StorageItem* ContactStorage::getItem( const QString& aItemId )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if( !iBackend )
    {
        return NULL;
    }

    SimpleItem* newItem = NULL;

    QContactLocalId id;
    id = QContactId::fromString (aItemId);
    QContact contact;

    iBackend->getContact( id, contact );
    QDateTime creationTime = iBackend->getCreationTime( contact );

    if( iFreshItems.contains( id.toString () ) )
    {
        qCDebug(lcSyncMLPlugin) << "Intercepted fresh item:" << id.toString ();
        iSnapshot.insert( id.toString (), creationTime );
        iFreshItems.removeOne( id.toString () );
    }

    QString contactData = iBackend->convertQContactToVCard( contact );

    if(!contactData.isEmpty())
    {
        newItem = new SimpleItem;
        newItem->setId(aItemId);
        newItem->setType(iProperties[STORAGE_DEFAULT_MIME_PROP]);
        newItem->write(0,contactData.toUtf8());
    }
    else
    {
        qCWarning(lcSyncMLPlugin) << "Contact does not exist:" << aItemId;
    }

    return newItem;
}

ContactStorage::OperationStatus ContactStorage::addItem(Buteo::StorageItem& aItem)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<Buteo::StorageItem*> items;
    items.append( &aItem );

    QList<ContactStorage::OperationStatus> status = addItems( items );

    return status.first();
}

QList<ContactStorage::OperationStatus> ContactStorage::addItems( const QList<Buteo::StorageItem*>& aItems )
{

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<ContactStorage::OperationStatus> storageErrorList;
    QDateTime currentTime = QDateTime::currentDateTime();

    if( !iBackend )
    {
        for ( int i = 0; i < aItems.size(); i++)
        {
            storageErrorList.append(STATUS_ERROR);
        }
        return storageErrorList;
    }

    QList<QString> contactsList;
    foreach(Buteo::StorageItem *item , aItems) {
        QByteArray data;
        item->read(0,item->getSize(),data);
        contactsList.append(QString::fromUtf8(data.data()));
    }

    QList<QString> contactsIdList;
    QMap<int, ContactsStatus> contactsErrorMap;
    bool retVal = iBackend->addContacts(contactsList, contactsErrorMap);

    if((contactsErrorMap.size()  != contactsList.size()) ||
        (contactsList.size() != aItems.size()))
    {

        qCWarning(lcSyncMLPlugin) << "Something Wrong with Batch Addition in Contacts Backend : " << retVal;
        qCDebug(lcSyncMLPlugin) << "contactsErrorMap.size() " << contactsErrorMap.size();
        qCDebug(lcSyncMLPlugin) << "contactsList.size()" << contactsList.size();
        qCDebug(lcSyncMLPlugin) << "aItems.size()" << aItems.size();

        for ( int i = 0; i < aItems.size(); i++) {
            storageErrorList.append(STATUS_ERROR);
        }

    }
    else
    {

        QMapIterator<int ,ContactsStatus> i(contactsErrorMap);
        int j = 0;
        while (i.hasNext()) {
            i.next();
            Buteo::StorageItem *item = aItems[j];
            item->setId(i.value().id);

            ContactStorage::OperationStatus status = mapErrorStatus(i.value().errorCode);

            if( status == STATUS_OK )
            {
                // This item was successfully added, so let's add it to the snapshot
                iSnapshot.insert( i.value().id, currentTime );
            }

            storageErrorList.append(status);
            j++;
        }

    }

    return storageErrorList;

}


ContactStorage::OperationStatus ContactStorage::modifyItem(Buteo::StorageItem& aItem)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        ContactStorage::OperationStatus status = STATUS_ERROR;

        if(iBackend ) {
                QString strID = aItem.getId();
                QByteArray data;
                aItem.read( 0, aItem.getSize(), data );
                QString Contact = QString::fromUtf8( data );
                qCDebug(lcSyncMLPlugin) << "Modifying an Item with data : " << Contact;
                qCDebug(lcSyncMLPlugin) << "Modifying an Item with ID : "  << strID;
                QContactManager::Error error = iBackend->modifyContact(strID ,Contact);
                status = mapErrorStatus(error);
                qCDebug(lcSyncMLPlugin) << "After Modification String ID  is " << strID;
        }

        return status;
}

QList<ContactStorage::OperationStatus> ContactStorage::modifyItems(const QList<Buteo::StorageItem *> &aItems)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        QList<ContactStorage::OperationStatus> storageErrorList;

        qCDebug(lcSyncMLPlugin) << "Items to Modify :"  << aItems.size();

        if(iBackend) {

        QStringList contactsList;
        QStringList contactsIdList;
                foreach(Buteo::StorageItem *item , aItems) {
                        QByteArray data;
                        item->read(0,item->getSize(),data);
                        contactsList.append(QString::fromUtf8(data.data()));
                        contactsIdList.append(item->getId());
                }

        QMap<int, ContactsStatus> contactsErrorMap =
                iBackend->modifyContacts(contactsList, contactsIdList);

                if(contactsErrorMap.size()  != contactsList.size())
                {

                    qCWarning(lcSyncMLPlugin) << "Something Wrong with Batch Modification in Contacts Backend";
                        qCDebug(lcSyncMLPlugin) << "contactsErrroMap.size() " << contactsErrorMap.size();
                        qCDebug(lcSyncMLPlugin) << "contactsList.size()" << contactsList.size();
                        for ( int i = 0; i < aItems.size(); i++) {
                            storageErrorList.append(STATUS_ERROR);
                        }

                } else  {

            QMapIterator<int, ContactsStatus> i(contactsErrorMap);
                        int j = 0;
                        while (i.hasNext()) {
                                i.next();
                                Buteo::StorageItem *item = aItems[j];
                                item->setId(i.value().id);
                                qCDebug(lcSyncMLPlugin) << "Id set in Storage " << item->getId();
                                storageErrorList.append(mapErrorStatus(i.value().errorCode));
                                j++;
                        }

                } // end if  contactsErrroMap.size()  != contactsList.size()

        } else {

                for ( int i = 0; i < aItems.size(); i++) {
                        storageErrorList.append(STATUS_ERROR);
                }

        }
        return storageErrorList;
}

ContactStorage::OperationStatus ContactStorage::deleteItem( const QString& aItemId )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<QString> itemIds;
    itemIds.append( aItemId );

    QList<ContactStorage::OperationStatus> status = deleteItems( itemIds );

    return status.first();
}

QList<ContactStorage::OperationStatus> ContactStorage::deleteItems(const QList<QString>& aItemIds )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<ContactStorage::OperationStatus> statusList;
    QDateTime currentTime = QDateTime::currentDateTime();

    if( !iBackend )
    {
        for ( int i = 0; i < aItemIds.size(); i++)
        {
            statusList.append(STATUS_ERROR);
        }
        return statusList;
    }

    QMap<int, ContactsStatus> errorMap = iBackend->deleteContacts(aItemIds);
    if(errorMap.size() !=aItemIds.size() )
    {
        qCWarning(lcSyncMLPlugin) << "Something wrong with batch deletion of Contacts";
        qCDebug(lcSyncMLPlugin) << "contactsErrroMap.size() " << errorMap.size();
        qCDebug(lcSyncMLPlugin) << "contactsList.size()" << aItemIds.size();
        for ( int i = 0; i < aItemIds.size(); i++)
        {
            statusList.append(STATUS_ERROR);
        }
    }
    else
    {
        QList<QString> itemIds;
        QList<QDateTime> creationTimes;
        QList<QDateTime> deletionTimes;

        QMapIterator<int, ContactsStatus> i(errorMap);
        int j = 0;
        while( i.hasNext() )
        {
            i.next();
            ContactStorage::OperationStatus status = mapErrorStatus( i.value().errorCode );

            if( status == STATUS_OK )
            {
                // This item was successfully deleted, so let's remove it from the snapshot and
                // add it to the deleted items.

                QString itemId = aItemIds[j];

                itemIds.append( itemId );
                creationTimes.append( iSnapshot.value( itemId ));
                iSnapshot.remove( itemId );
                deletionTimes.append( currentTime );
            }

            statusList.append( status );
            j++;
        }

        if( !itemIds.isEmpty() )
        {
            iDeletedItems.addDeletedItems( itemIds, creationTimes, deletionTimes );
        }
    }


    return statusList;
}

bool ContactStorage::doInitItemAnalysis()
{
    /* Items are analyzed using two sets of items: those that existed at the end
     * of the previous time this plugin was uninitiated (called snapshot), and those
     * that exists now (called backend). The point of the analysis is to divide the
     * items into 3 groups:
     * 1) Items that exist only in the snapshot. These items have been deleted, and
     *    should be moved to the list of deleted items. The creation time of these
     *    items is known, as they are stored in the database.
     * 2) Items that exist both in the snapshot and the backend. Since there's no
     *    change in the lifetime of these items, they do not need any special
     *    attention.
     * 3) Items that exist only in the backed. These items have been created recently,
     *    so their creation time needs to be fetched from the backend before they
     *    are added to snapshot we are storing to database during uninit(). We could
     *    retrieve creation times here, but because these items are usually accessed
     *    anyway during a session, we will just add them to a list of "fresh items"
     *    which will be checked whenever a contact is retrieved from backend.
     *    The creation times of all remaining "fresh" items which were not accessed
     *    during the session will be retrieved in uninit()
     *
     * After items have been analyzed, the in-memory snapshot will be identical to
     * backend. In-memory snapshot is updated whenever we add or delete items. In
     * the item analysis of uninit() we therefore have an up-to-date snapshot of the
     * contents of the backend. Changes that external application do the beckend
     * during the sync session will get discovered in the next session.
     */

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Q_ASSERT( iBackend );
    iSnapshot.clear();
    iFreshItems.clear();

    QDateTime currentTime = QDateTime::currentDateTime();
    QMap<QString, QDateTime> snapshot;
    QList<QString> backend;
    QList<QString> freshItems;

    // ** Retrieve previous snapshot from db and convert to QHash
    QList<QString> snapshotItems;
    QList<QDateTime> snapshotCreationTimes;

    if( !iDeletedItems.getSnapshot(snapshotItems, snapshotCreationTimes) ) {
        return false;
    }

    for( int i = 0; i < snapshotItems.count(); ++i )
    {
        snapshot.insert( snapshotItems[i], snapshotCreationTimes[i] );
    }

    // ** Retrieve backend
    QList<QContactId> backendIds = iBackend->getAllContactIds();
    foreach (const QContactId id, backendIds) {
        backend << id.toString ();
    }

    qCDebug(lcSyncMLPlugin) << "Found" << snapshot.count() << "items from snapshot";
    qCDebug(lcSyncMLPlugin) << "Found" << backend.count() << "items from backend";

    QList<QString> itemIds;
    QList<QDateTime> creationTimes;
    QList<QDateTime> deletionTimes;

    // ** Find items only in the snapshot and mark them as deleted
    QMutableMapIterator<QString, QDateTime> i( snapshot );

    while( i.hasNext() )
    {
        i.next();
        if( !backend.contains( i.key() ) )
        {
            itemIds.append( i.key() );
            creationTimes.append( i.value() );
            deletionTimes.append( currentTime );
            i.remove();
        }
    }

    qCDebug(lcSyncMLPlugin) << "Detected" << itemIds.count() <<"deleted items";

    if( !itemIds.isEmpty() )
    {
        iDeletedItems.addDeletedItems( itemIds, creationTimes, deletionTimes );
    }

    // ** Find items only in backend and mark them as fresh items

    for( int i = 0; i < backend.count(); ++i )
    {
        if( !snapshot.contains( backend[i] ) )
        {
            freshItems.append( backend[i] );
            snapshot.insert( backend[i], QDateTime() );
        }
    }

    iSnapshot = snapshot;
    iFreshItems = freshItems;

    qCDebug(lcSyncMLPlugin) << "Detected" << iFreshItems.count() <<"fresh items";

    return true;

}

bool ContactStorage::doUninitItemAnalysis()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Q_ASSERT( iBackend );

    // ** Retrieve creation times for the remaining "fresh" items from the backend

    if( !iFreshItems.isEmpty() )
    {
        qCDebug(lcSyncMLPlugin) << "Retrieving creation times for" << iFreshItems.count() << "fresh items";

        QList<QContactId> idList;
        foreach (const QString freshItem, iFreshItems) {
            idList << QContactId::fromString (freshItem);
        }
        QList<QDateTime> freshCreationTimes = iBackend->getCreationTimes( idList );

        for( int i = 0; i < iFreshItems.count(); ++i )
        {
            iSnapshot.insert( iFreshItems[i], freshCreationTimes[i] );
        }
    }

    // ** Store the snapshot to the database

    QTime timer;
    timer.start();
    QList<QString> intIds = iSnapshot.keys();

    QList<QString> ids;
    for( int i = 0; i < intIds.count(); ++i )
    {
        ids.append( intIds[i] );
    }

    QList<QDateTime> creationTimes = iSnapshot.values();

    qCDebug(lcSyncMLPlugin) << "Storing" << ids.count() <<"items to snapshot";

    iDeletedItems.setSnapshot( ids, creationTimes );

    iSnapshot.clear();
    iFreshItems.clear();

    return true;
}


QList<Buteo::StorageItem*> ContactStorage::getStoreList(QList<QContactLocalId> &aStrIDList)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        QList<Buteo::StorageItem*> itemList;


    if (iBackend != NULL) {
        QMap<QString,QString> idDataMap;
        iBackend->getContacts(aStrIDList, idDataMap);
        QMapIterator<QString, QString > iter(idDataMap);

        while (iter.hasNext()) {
            iter.next();
            SimpleItem* item = convertVcardToStorageItem(QContactId::fromString (iter.key()), iter.value());
            if (item  != NULL) {
                itemList.append(item);
            }

        }
    }

    return itemList;
}

QByteArray ContactStorage::getCtCaps( const QString& aFilename ) const
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QFile ctCapsFile( SyncMLConfig::getXmlDataPath() + aFilename  );
    QByteArray ctCaps;

    if( ctCapsFile.open(QIODevice::ReadOnly)) {
        ctCaps = ctCapsFile.readAll();
        ctCapsFile.close();
    } else {
        qCWarning(lcSyncMLPlugin) << "Failed to open CTCaps file for contacts storage:" << aFilename ;
    }

    return ctCaps;
}

ContactStorage::OperationStatus ContactStorage::mapErrorStatus(
        const QContactManager::Error &aContactError) const
{
        ContactStorage::OperationStatus iStorageStatus = STATUS_ERROR;
        switch(aContactError) {
        case QContactManager::NoError:
                iStorageStatus = STATUS_OK;
                break;

        case QContactManager::DoesNotExistError:
                iStorageStatus = STATUS_NOT_FOUND;
                break;

        case QContactManager::AlreadyExistsError:
                iStorageStatus = STATUS_DUPLICATE;
                break;

        case QContactManager::InvalidDetailError:
        case QContactManager::VersionMismatchError:
        case QContactManager::InvalidContactTypeError:
                iStorageStatus = STATUS_INVALID_FORMAT;
                break;

        case QContactManager::OutOfMemoryError:
                iStorageStatus = STATUS_STORAGE_FULL;
                break;

        case QContactManager::LimitReachedError:
                iStorageStatus = STATUS_OBJECT_TOO_BIG;
                break;

        case QContactManager::InvalidRelationshipError:
        case QContactManager::LockedError:
        case QContactManager::DetailAccessError:
        case QContactManager::PermissionsError:
        case QContactManager::NotSupportedError:
        case QContactManager::BadArgumentError:
        case QContactManager::UnspecifiedError:
        default:
                iStorageStatus = STATUS_ERROR;
                break;
        }
        return iStorageStatus;
}

/*!
    \fn ContactStorage::convertVcardToStorageItem(const QContactLocalId, const QString&)
 */
SimpleItem* ContactStorage::convertVcardToStorageItem(const QContactLocalId aItemKey,
                                                      const QString& aItemData)
{

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    SimpleItem* storageItem = new SimpleItem;

    if(storageItem != NULL) {
        qCDebug(lcSyncMLPlugin) << "ID is " << aItemKey;
        qCDebug(lcSyncMLPlugin) << "Data is " << aItemData;
        storageItem->write( 0, aItemData.toUtf8() );
        storageItem->setId(aItemKey.toString());
        storageItem->setType(iProperties[STORAGE_DEFAULT_MIME_PROP]);
    }
    else {
        qCWarning(lcSyncMLPlugin) << "Memory Allocation Failed";
    }

    return storageItem;
}


Buteo::StoragePlugin* ContactsStoragePluginLoader::createPlugin(const QString& aPluginName)
{
    return new ContactStorage(aPluginName);
}

