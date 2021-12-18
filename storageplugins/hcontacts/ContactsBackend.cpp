/*
 * This file is part of buteo-sync-plugins package
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2014 Jolla Ltd.
 *
 * Contributors: Sateesh Kavuri <sateesh.kavuri@nokia.com>
 *               Bea Lam <bea.lam@jolla.com>
 *               Valério Valério <valerio.valerio@jolla.com>
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

#include "ContactsBackend.h"
#include "ContactBuilder.h"

#include <seasideimport.h>
#include <seasidepropertyhandler.h>

#include "SyncMLPluginLogging.h"

#include <QVersitContactExporter>
#include <QVersitContactImporter>
#include <QVersitReader>
#include <QVersitWriter>
#include <QContactTimestamp>
#include <QContactSyncTarget>
#include <QContactDetailFilter>
#include <QContactUnionFilter>
#include <QContactInvalidFilter>

#include <qtcontacts-extensions.h>
#include <QContactOriginMetadata>

#include <QContactIdFilter>

#include <QBuffer>
#include <QSet>


ContactsBackend::ContactsBackend(QVersitDocument::VersitType aVCardVer, const QString &syncTarget, const QString &originId) :
iReadMgr(NULL), iWriteMgr(NULL), iVCardVer(aVCardVer) //CID 26531
    , iSyncTarget(syncTarget)
    , iOriginId(originId)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

ContactsBackend::~ContactsBackend()
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

bool ContactsBackend::init()
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        QMap<QString, QString> params;
        params.insert(QStringLiteral("privileged"), QStringLiteral("true"));
        iReadMgr = new QContactManager(QLatin1String("org.nemomobile.contacts.sqlite"), params);

        iWriteMgr = new QContactManager(QLatin1String("org.nemomobile.contacts.sqlite"));

        return (iReadMgr != NULL && iWriteMgr != NULL);
}

bool ContactsBackend::uninit()
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        delete iReadMgr;
        iReadMgr = NULL;

        delete iWriteMgr;
        iWriteMgr = NULL;

        return true;
}



QList<QContactLocalId> ContactsBackend::getAllContactIds()
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    QList<QContactLocalId> contactIDs;

    if (iReadMgr != NULL) {
        contactIDs = iReadMgr->contactIds();
    } else {
        qCWarning(lcSyncMLPlugin) << "Contacts backend not available";
    }

    return contactIDs;
}

QList<QContactLocalId> ContactsBackend::getAllNewContactIds(const QDateTime &aTimeStamp)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        qCDebug(lcSyncMLPlugin) << "Retrieve New Contacts Since " << aTimeStamp;

        QList<QContactLocalId> idList;
        const QContactChangeLogFilter::EventType eventType =
                        QContactChangeLogFilter::EventAdded;

        getSpecifiedContactIds(eventType, aTimeStamp, idList);

        return idList;
}

QList<QContactLocalId> ContactsBackend::getAllModifiedContactIds(const QDateTime &aTimeStamp)
{

        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        qCDebug(lcSyncMLPlugin) << "Retrieve Modified Contacts Since " << aTimeStamp;

        QList<QContactLocalId> idList;
        const QContactChangeLogFilter::EventType eventType =
                        QContactChangeLogFilter::EventChanged;

        getSpecifiedContactIds(eventType, aTimeStamp, idList);

        return idList;
}

QList<QContactLocalId> ContactsBackend::getAllDeletedContactIds(const QDateTime &aTimeStamp)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        qCDebug(lcSyncMLPlugin) <<  "Retrieve Deleted Contacts Since " << aTimeStamp;

        QList<QContactLocalId> idList;
        const QContactChangeLogFilter::EventType eventType =
                        QContactChangeLogFilter::EventRemoved;

        getSpecifiedContactIds(eventType, aTimeStamp, idList);

        return idList;
}

bool ContactsBackend::addContacts( const QStringList& aContactDataList,
                                   QMap<int, ContactsStatus>& aStatusMap )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Q_ASSERT( iReadMgr );
    Q_ASSERT( iWriteMgr );

    QList<QVersitDocument> documents = convertVCardListToVersitDocumentList(aContactDataList);
    if (documents.isEmpty()) {
        qCWarning(lcSyncMLPlugin) << "invalid sync data, aborting";
        return false;
    }

    qCDebug(lcSyncMLPlugin) <<  "converted" << aContactDataList.size() << "concatenated vCards into" << documents.size() << "versit documents";

    int newCount = 0;
    int updatedCount = 0;
    int ignoredCount = 0;
    ContactBuilder builder(iWriteMgr, iSyncTarget, iOriginId, ContactBuilder::FilterRequiredMode);
    QList<QContact> contactList = SeasideImport::buildImportContacts(
                                                     documents,
                                                     &newCount,
                                                     &updatedCount,
                                                     &ignoredCount,
                                                     &builder);
    qCDebug(lcSyncMLPlugin) <<  "imported" << contactList.size() << "contacts from" << documents.size() << "versit documents";
    if (contactList.size() != documents.size()) {
        qCWarning(lcSyncMLPlugin) << "internal error: could not convert every versit document to a contact:" << contactList.size() << "<" << documents.size();
        return false;
    }

    prepareContactSave(&contactList);
    qCDebug(lcSyncMLPlugin) <<  "New contacts:" << newCount << "Updated contacts:" << updatedCount;

    QMap<int, QContactManager::Error> errorMap;
    bool retVal = iWriteMgr->saveContacts(&contactList, &errorMap);
    if (!retVal) {
        qCWarning(lcSyncMLPlugin) <<  "Errors reported while saving contacts:" << iWriteMgr->error();
    }

    // Populate the status value for each addition item (document).
    ContactsStatus status;
    for (int i = 0; i < documents.size(); ++i) {
        status.id = contactList.at(i).id().toString();
        status.errorCode = errorMap.value(i, QContactManager::NoError);
        aStatusMap.insert(i, status);
    }

    return retVal;
}

QContactManager::Error ContactsBackend::modifyContact(const QString &aID, const QString &aContact)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    qCDebug(lcSyncMLPlugin) <<  "Modifying a Contact with ID" << aID;

    QContactManager::Error modificationStatus = QContactManager::UnspecifiedError;

    if (iWriteMgr == NULL) {
        qCWarning(lcSyncMLPlugin) << "Contacts backend not available";
    } else {
        QContact oldContactData;
        getContact(QContactId::fromString (aID), oldContactData);

        QList<QVersitDocument> documents = convertVCardListToVersitDocumentList(QStringList() << aContact);
        if (documents.size() < 1) {
            qCWarning(lcSyncMLPlugin) << "Not a valid vCard:" << aContact;
            return QContactManager::UnspecifiedError;
        }

        int newCount = 0;
        int updatedCount = 0;
        int ignoredCount = 0;
        ContactBuilder builder(iWriteMgr, iSyncTarget, iOriginId, ContactBuilder::NoFilterRequiredMode);
        QList<QContact> contacts = SeasideImport::buildImportContacts(
                                                         documents,
                                                         &newCount,
                                                         &updatedCount,
                                                         &ignoredCount,
                                                         &builder);

        if (contacts.size() < 1) {
            qCWarning(lcSyncMLPlugin) << "Unable to convert vCard to contact:" << aContact;
            return QContactManager::UnspecifiedError;
        } else if (contacts.size() > 1) {
            qCWarning(lcSyncMLPlugin) << "vCard encodes multiple contacts when one is expected:" << aContact;
            // just process the first one, ignore the rest.
        }

        QContact newContactData = contacts.first();
        newContactData.setId(oldContactData.id());
        bool modificationOk = iWriteMgr->saveContact(&oldContactData);
        modificationStatus = iWriteMgr->error();
        if(!modificationOk) {
            qCWarning(lcSyncMLPlugin) << "Contact Modification Failed";
        }
    }

    return modificationStatus;
}

QMap<int,ContactsStatus> ContactsBackend::modifyContacts(
    const QStringList &aVCardDataList, const QStringList &aContactIdList)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Q_ASSERT (iWriteMgr);
    ContactsStatus status;

    QMap<int,QContactManager::Error> errors;
    QMap<int,ContactsStatus> statusMap;

    int newCount = 0;
    int updatedCount = 0;
    int ignoredCount = 0;
    QList<QVersitDocument> documents = convertVCardListToVersitDocumentList(aVCardDataList);
    qCDebug(lcSyncMLPlugin) <<  "converted" << aVCardDataList.size() << "concatenated vCards into" << documents.size() << "versit documents";
    ContactBuilder builder(iWriteMgr, iSyncTarget, iOriginId, ContactBuilder::NoFilterRequiredMode);
    QList<QContact> contacts = SeasideImport::buildImportContacts(
                                                     documents,
                                                     &newCount,
                                                     &updatedCount,
                                                     &ignoredCount,
                                                     &builder);

    qCDebug(lcSyncMLPlugin) <<  "imported" << contacts.size() << "contacts from" << documents.size() << "versit documents";
    if (contacts.size() != aContactIdList.size()) {
        qCWarning(lcSyncMLPlugin) << "internal error: could not convert every versit document to a contact:" << contacts.size() << "<" << documents.size();
    } else {
        for (int i = 0; i < contacts.size(); i++) {
            qCDebug(lcSyncMLPlugin) <<  "Id of the contact to be replaced" << aContactIdList.at(i);
            QContactLocalId uniqueContactItemID = QContactId::fromString (aContactIdList.at(i));
            contacts[i].setId(uniqueContactItemID);
            qCDebug(lcSyncMLPlugin) <<  "Replacing item's ID " << contacts.at(i);
        }

        if(iWriteMgr->saveContacts(&contacts , &errors)) {
            qCDebug(lcSyncMLPlugin) <<  "Batch Modification of Contacts Succeeded";
        } else {
            qCDebug(lcSyncMLPlugin) <<  "Batch Modification of Contacts Failed";
        }

        // QContactManager will populate errorMap only for errors, but we use this as a status map,
        // so populate NoError if there's no error.
        // TODO QContactManager populates indices from the qContactList, but we populate keys, is this OK?
        for (int i = 0; i < contacts.size(); i++) {
            QContactLocalId contactId = contacts.at(i).id();
            status.id = contactId.toString ();
            if( !errors.contains(i) ) {
                qCDebug(lcSyncMLPlugin) <<  "No error for contact with id " << contactId << " and index " << i;
                status.errorCode = QContactManager::NoError;
            } else {
                qCDebug(lcSyncMLPlugin) <<  "contact with id " << contactId << " and index " << i <<" is in error";
                QContactManager::Error errorCode = errors.value(i);
                status.errorCode = errorCode;
            }
            statusMap.insert(i, status);
        }
    }

    return statusMap;
}

QMap<int , ContactsStatus> ContactsBackend::deleteContacts(const QStringList &aContactIDList)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    ContactsStatus status;
    QMap<int , QContactManager::Error> errors;
    QMap<int , ContactsStatus> statusMap;

    if (iWriteMgr == NULL) {
        for (int i=0; i < aContactIDList.size(); i++) {
            errors.insert(i, QContactManager::UnspecifiedError);
        }

        qCWarning(lcSyncMLPlugin) << "Contacts backend not available";
    }
    else {
        QList<QContactLocalId> qContactIdList;
        foreach (QString id, aContactIDList ) {
            qContactIdList.append(QContactLocalId::fromString (id));
        }

        if(iWriteMgr->removeContacts(qContactIdList , &errors)) {
            qCDebug(lcSyncMLPlugin) <<  "Successfully Removed all contacts ";
        }
        else {
            qCWarning(lcSyncMLPlugin) << "Failed Removing Contacts";
        }

        // QContactManager will populate errorMap only for errors, but we use this as a status map,
        // so populate NoError if there's no error.
        // TODO QContactManager populates indices from the qContactList, but we populate keys, is this OK?
        for (int i = 0; i < qContactIdList.size(); i++) {
            QContactLocalId contactId = qContactIdList.value(i);
            status.id = contactId.toString ();

            if( !errors.contains(i) )
            {
                qCDebug(lcSyncMLPlugin) <<  "No error for contact with id " << contactId << " and index " << i;
                status.errorCode = QContactManager::NoError;
            }
            else
            {
                qCDebug(lcSyncMLPlugin) <<  "contact with id " << contactId << " and index " << i <<" is in error";
                QContactManager::Error errorCode = errors.value(i);
                status.errorCode = errorCode;
            }
            statusMap.insert(i, status);
        }
    }

        return statusMap;
}

void ContactsBackend::prepareContactSave(QList<QContact> *contactList)
{
    if (!iSyncTarget.isEmpty() || !iOriginId.isEmpty()) {
        for (int i=0; i<contactList->count(); i++) {
            QContact *contact = &((*contactList)[i]);
            if (!iSyncTarget.isEmpty()) {
                QContactSyncTarget syncTarget = contact->detail<QContactSyncTarget>();
                syncTarget.setSyncTarget(iSyncTarget);
                contact->saveDetail(&syncTarget);
            }
            if (!iOriginId.isEmpty()) {
                QContactOriginMetadata originMetaData = contact->detail<QContactOriginMetadata>();
                originMetaData.setId(iOriginId);
                contact->saveDetail(&originMetaData);
            }
            Q_FOREACH (QContactDetail det, contact->details()) {
                det.setValue(QContactDetail__FieldModifiable, true);
                contact->saveDetail(&det);
            }
        }
    }
}

QList<QVersitDocument> ContactsBackend::convertVCardListToVersitDocumentList(const QStringList &aVCardList)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<QVersitDocument> retn;
    Q_FOREACH (const QString &vCard, aVCardList) {
        // remove any characters after the END:VCARD stanza.
        // importantly, we do NOT ensure it ends in \r\n or \r\n\r\n
        // TODO: fix QVersitReader to strip \r\n and \r\n\r\n endings.
        int endIdx = vCard.lastIndexOf(QStringLiteral("END:VCARD"), -1, Qt::CaseInsensitive);
        QString modifiedVCard = vCard.mid(0, endIdx + 9); /* 9 = strlen("END:VCARD") */

        // convert the vCard to a contact.
        QVersitReader versitReader(modifiedVCard.toUtf8());
        versitReader.startReading();
        versitReader.waitForFinished();

        QList<QVersitDocument> results = versitReader.results();
        if (results.size() == 0) {
            qCWarning(lcSyncMLPlugin) << "Unable to convert vCard to versit document:" << versitReader.error() << ":";
            QStringList erroneousVCardLines = modifiedVCard.split('\n', QString::KeepEmptyParts);
            Q_FOREACH(QString line, erroneousVCardLines) {
                if (line.contains(':') || line.trimmed().isEmpty()) {
                    line.replace('\r', "<CR>");
                    line.append("<LF>");
                    qCWarning(lcSyncMLPlugin) << line;
                }
            }
            return QList<QVersitDocument>();
        } else if (versitReader.results().size() > 1) {
            qCWarning(lcSyncMLPlugin) << "Multiple contacts from single vCard:" << modifiedVCard;
        }

        retn.append(results.first());
    }

    return retn;
}

QString ContactsBackend::convertQContactToVCard(const QContact &aContact)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        QList<QContact> contactsList;
        contactsList.append (aContact);

        QVersitContactExporter contactExporter;

        QSet<QContactDetail::DetailType> ignoredDetailTypes = QSet<QContactDetail::DetailType>()
                                                              << QContactDetail::TypeGlobalPresence
                                                              << QContactDetail::TypePresence
                                                              << QContactDetail::TypeOnlineAccount
                                                              << QContactDetail::TypeVersion
                                                              << QContactDetail::TypeSyncTarget
                                                              << QContactDetail::TypeRingtone;
        SeasidePropertyHandler handler(ignoredDetailTypes);
        contactExporter.setDetailHandler(&handler);

        QString vCard;
        bool contactsExported = contactExporter.exportContacts(contactsList, iVCardVer);
        if (contactsExported){
                QList<QVersitDocument> versitDocumentList;
                versitDocumentList = contactExporter.documents();

                QBuffer writeBuf;
                writeBuf.open(QBuffer::ReadWrite);

                QVersitWriter writer;
                writer.setDevice(&writeBuf);

                if (!writer.startWriting(versitDocumentList)) {
                        qCCritical(lcSyncMLPlugin) << "Error While writing -- " << writer.error();
                }

                if (writer.waitForFinished()) {
                        vCard = writeBuf.buffer();
                }

                writeBuf.close();
        }
        return vCard;
}

QMap<QString, QString> ContactsBackend::convertQContactListToVCardList(
    const QList<QContact> & aContactList)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
        QMap<QString, QString> idDataMap;

        foreach (QContact contact, aContactList) {
                idDataMap[contact.id ().toString ()] = convertQContactToVCard(contact);
        }

        return idDataMap;
}

void ContactsBackend::getSpecifiedContactIds(const QContactChangeLogFilter::EventType aEventType,
                const QDateTime& aTimeStamp, QList<QContactLocalId>& aIdList)
{
        FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

        QContactChangeLogFilter filter(aEventType);
        filter.setSince(aTimeStamp);

    aIdList = iReadMgr->contactIds(filter);

    // Fetch the ids from aIdList
    QList<QString> strIdList;
    foreach (const QContactId& id, aIdList) {
        strIdList << id.toString ();
    }

    // Filter out ids for items that were added after the specified time.
    if (aEventType != QContactChangeLogFilter::EventAdded)
    {
        filter.setEventType(QContactChangeLogFilter::EventAdded);
        QList<QContactLocalId> addedList = iReadMgr->contactIds(filter);

        QList<QString> addedStrIdList;
        foreach (const QContactId& id, addedList) {
            addedStrIdList << id.toString ();
        }

        foreach (const QString addedId, addedStrIdList) {
            strIdList.removeAll (addedId);
        }

        foreach (const QContactLocalId &id, addedList)
        {
            aIdList.removeAll(id);
        }
    }

        // This is a defensive procedure to prevent duplicate items being sent.
        // QSet does not allow duplicates, thus transforming QList to QSet and back
        // again will remove any duplicate items in the original QList.
    int originalIdCount = strIdList.size ();
    QSet<QString> idSet = strIdList.toSet ();
    int idCountAfterDupRemoval = idSet.size ();
    strIdList = idSet.toList ();

    qCDebug(lcSyncMLPlugin) << "Item IDs found (returned / incl. duplicates): " << idCountAfterDupRemoval << "/" << originalIdCount;

    if (originalIdCount != idCountAfterDupRemoval) {
        qCWarning(lcSyncMLPlugin) << "Contacts backend returned duplicate items for requested list";
        qCWarning(lcSyncMLPlugin) << "Duplicate item IDs have been removed";
    } // no else

    // Convert strIdList to aIdList (QContactId)
    aIdList.clear();
    foreach (const QString &id, strIdList) {
        aIdList << QContactId::fromString (id);
    }
}

QDateTime ContactsBackend::lastModificationTime(const QContactLocalId &aContactId)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QDateTime lastModificationTime = QDateTime::fromTime_t(0);

    if (iReadMgr == NULL) {
        qCWarning(lcSyncMLPlugin) << "Contacts backend not available";
    }
    else {
        QContact contact;
        getContact(aContactId, contact);
        QContactTimestamp contactTimestamps;
        contactTimestamps = contact.detail (QContactTimestamp::Type);
        lastModificationTime = contactTimestamps.lastModified();
    }

    return lastModificationTime;
}

/*!
    \fn ContactsBackend::getContact(QContactLocalId aContactId)
 */
void ContactsBackend::getContact(const QContactLocalId& aContactId, QContact& aContact)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<QContactLocalId> contactId;
    contactId.append(aContactId);
    QList<QContact>        returnedContacts;

    getContacts(contactId, returnedContacts);

    if (!returnedContacts.isEmpty()) {
        aContact = returnedContacts.first();
    }
}

/*!
    \fn ContactsBackend::getContacts(QContactLocalId aContactId)
 */
void ContactsBackend::getContacts(const QList<QContactLocalId>& aContactIds,
                                  QList<QContact>& aContacts)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QContactIdFilter contactFilter;
    contactFilter.setIds(aContactIds);

    if (iReadMgr != NULL) {
        aContacts = iReadMgr->contacts(contactFilter);
    }
}

void ContactsBackend::getContacts(const QList<QContactLocalId>&  aIdsList,
                                  QMap<QString,QString>& aDataMap)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QList<QContact> returnedContacts;

    // As this is an overloaded convenience function, these two functions
    // are utilized to get contacts from the backend and to convert them
    // to vcard format.
    getContacts(aIdsList, returnedContacts);
    aDataMap = convertQContactListToVCardList(returnedContacts);
}

QDateTime ContactsBackend::getCreationTime( const QContact& aContact )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QContactTimestamp contactTimestamp = aContact.detail<QContactTimestamp>();

    return contactTimestamp.created();
}

QList<QDateTime> ContactsBackend::getCreationTimes( const QList<QContactLocalId>& aContactIds )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    Q_ASSERT( iReadMgr );

    /* Retrieve QContacts from backend based on id's in aContactsIds. Since we're only interested
     * in timestamps, set up fetch hint accordingly to speed up the operation.
     */
    QList<QDateTime> creationTimes;
    QList<QContact> contacts;

    QContactIdFilter contactFilter;
    contactFilter.setIds(aContactIds);

    QList<QContactDetail::DetailType> detailTypes;
    detailTypes << QContactTimestamp::Type;

    /* Set up fetch hints so that not all details of QContacts be fetched:
     * 1) Fetch only QContactTimestamp details
     * 2) Do not try to resolve contact relationships (siblings etc)
     * 3) Do not include action preferences of contacts
     * 4) Do not fetch binary blogs (avatar pictures etc)
     */
    QContactFetchHint contactHint;
    contactHint.setOptimizationHints( QContactFetchHint::NoRelationships |
                                      QContactFetchHint::NoActionPreferences |
                                      QContactFetchHint::NoBinaryBlobs );

    contactHint.setDetailTypesHint (detailTypes);

    QDateTime currentTime = QDateTime::currentDateTime();

    contacts = iReadMgr->contacts( contactFilter, QList<QContactSortOrder>(), contactHint );

    if( contacts.count() == aContactIds.count() )
    {
        for( int i = 0; i < aContactIds.count(); ++i )
        {
            QDateTime creationTime = currentTime;

            for( int a = 0; a < contacts.count(); ++a )
            {
                if( contacts[a].id().toString () == aContactIds[i].toString () )
                {
                    QContactTimestamp contactTimestamp = contacts[a].detail<QContactTimestamp>();
                    if( !contactTimestamp.created().isNull() &&
                        contactTimestamp.created().isValid() )
                    {
                        creationTime = contactTimestamp.created();
                    }
                    contacts.removeAt( a );
                    break;
                }
            }

            creationTimes.append( creationTime );
        }
    }
    else
    {
        qCWarning(lcSyncMLPlugin) << "Unable to fetch creation times";
        for( int i = 0; i < aContactIds.count(); ++i )
        {
            creationTimes.append( currentTime );
        }
    }

    return creationTimes;
}
