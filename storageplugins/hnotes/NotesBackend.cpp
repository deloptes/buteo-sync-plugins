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

#include "NotesBackend.h"

#include <extendedcalendar.h>
#include <sqlitestorage.h>
#include <QDir>

#include "SyncMLPluginLogging.h"

#include "SimpleItem.h"

// @todo: handle unicode notes better. For example S60 seems to send only ascii.
//        Ovi.com seems to send latin-1 in base64-encoded form. UTF-8 really should
//        be preferred here, but how would we know which format is given to us as
//        latin-1 and utf-8 are not compatible?

static const QString INCIDENCE_TYPE_JOURNAL( "Journal" );

NotesBackend::NotesBackend() : iCalendar( 0 ), iStorage( 0 )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

NotesBackend::~NotesBackend()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
}

bool NotesBackend::init( const QString& aNotebookName, const QString& aUid,
                         const QString &aMimeType )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    qCDebug(lcSyncMLPlugin) <<  "Notes backend using notebook" << aNotebookName << "And uuid" << aUid;

    if( aNotebookName.isEmpty() )
    {
        qCDebug(lcSyncMLPlugin) << "NoteBook Name to Sync is expected. It Cannot be Empty";
        return false;
    }

    iNotebookName = aNotebookName;
    iMimeType = aMimeType;

    iCalendar = mKCal::ExtendedCalendar::Ptr( new mKCal::ExtendedCalendar(QTimeZone::systemTimeZone()) );

    qCDebug(lcSyncMLPlugin) << "Creating Default Maemo Storage for Notes";
    iStorage = iCalendar->defaultStorage( iCalendar );

    bool opened = iStorage->open();

    if (!opened)
    {
        qCDebug(lcSyncMLPlugin) << "Calendar storage open failed";
    }

    mKCal::Notebook::Ptr openedNb;

    // If we have an Uid, we try to get the corresponding Notebook
    if (!aUid.isEmpty()) {
        openedNb = iStorage->notebook(aUid);

        // If we didn't get one, we create one and set its Uid
        if (!openedNb) {
            openedNb = mKCal::Notebook::Ptr(new mKCal::Notebook(aNotebookName,
                                                                "Synchronization Created Notebook for " + aNotebookName));
            if (!openedNb.isNull()) {
                openedNb->setUid(aUid);
                if (!iStorage->addNotebook(openedNb)) {
                    qCWarning(lcSyncMLPlugin) << "Failed to add notebook to storage";
                }
            }
        }
    }
    // If we didn't have an Uid or the creation above failed,
    // we use the default notebook
    if (openedNb.isNull()) {
        qCDebug(lcSyncMLPlugin) << "Using default notebook";
        openedNb = iStorage->defaultNotebook();
        if(openedNb.isNull())
        {
            qCDebug(lcSyncMLPlugin) << "No default notebook exists, creating one";
            openedNb = iStorage->createDefaultNotebook();
        }
    }

    bool loaded = false;
    if(opened)
    {
        qCDebug(lcSyncMLPlugin) << "Loading all incidences from::" << openedNb->uid();
        loaded = iStorage->loadNotebookIncidences(openedNb->uid());
        if(!loaded)
        {
            qCWarning(lcSyncMLPlugin) << "Failed to load calendar";
        }
    }

    if (opened && loaded && !openedNb.isNull())
    {
        iNotebookName = openedNb->uid();

        qCDebug(lcSyncMLPlugin) << "Calendar initialized for notes";
        return true;
    }
    else
    {
        qCWarning(lcSyncMLPlugin) << "Not able to initialize calendar";

        iStorage.clear();

        qCDebug(lcSyncMLPlugin) << "Storage deleted";

        iCalendar.clear();

        qCDebug(lcSyncMLPlugin) << "Calendar deleted";

        return false;
    }
}


bool NotesBackend::uninit()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    if( iStorage ) {
        iStorage->close();
        iStorage.clear();
    }

    if( iCalendar ) {
        iCalendar->close();
        iCalendar.clear();
    }

    return true;
}

bool NotesBackend::getAllNotes( QList<Buteo::StorageItem*>& aItems )
{

    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->allIncidences( &incidences, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) <<  "Could not retrieve all notes";
        return false;
    }

    retrieveNoteItems( incidences, aItems );

    return true;

}

bool NotesBackend::getAllNoteIds( QList<QString>& aItemIds )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->allIncidences( &incidences, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) <<  "Could not retrieve all notes";
        return false;
    }

    retrieveNoteIds( incidences, aItemIds );

    return true;
}

bool NotesBackend::getNewNotes( QList<Buteo::StorageItem*>& aNewItems, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->insertedIncidences( &incidences, aTime, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) <<  "Could not retrieve new notes";
        return false;
    }

    retrieveNoteItems( incidences, aNewItems );

    return true;
}

bool NotesBackend::getNewNoteIds( QList<QString>& aNewItemIds, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->insertedIncidences( &incidences, aTime, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) <<  "Could not retrieve new notes";
        return false;
    }

    retrieveNoteIds( incidences, aNewItemIds );

    return true;
}

bool NotesBackend::getModifiedNotes( QList<Buteo::StorageItem*>& aModifiedItems, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->modifiedIncidences( &incidences, aTime, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) << "Could not retrieve modified notes";
        return false;
    }

    retrieveNoteItems( incidences, aModifiedItems );

    return true;
}

bool NotesBackend::getModifiedNoteIds( QList<QString>& aModifiedItemIds, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->modifiedIncidences( &incidences, aTime, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) << "Could not retrieve modified notes";
        return false;
    }

    retrieveNoteIds( incidences, aModifiedItemIds );

    return true;
}

bool NotesBackend::getDeletedNoteIds( QList<QString>& aDeletedItemIds, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    KCalendarCore::Incidence::List incidences;

    if( !iStorage->deletedIncidences( &incidences, aTime, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) << "Could not retrieve modified notes";
        return false;
    }

    retrieveNoteIds( incidences, aDeletedItemIds );

    return true;

}

Buteo::StorageItem* NotesBackend::newItem()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    return new SimpleItem;
}

Buteo::StorageItem* NotesBackend::getItem( const QString& aItemId )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    iStorage->load ( aItemId );
    KCalendarCore::Incidence::Ptr item = iCalendar->incidence( aItemId );

    if( !item ) {
        qCWarning(lcSyncMLPlugin) << "Could not find item:" << aItemId;
        return NULL;
    }

    Buteo::StorageItem* storageItem = newItem();
    storageItem->setId( item->uid() );
    storageItem->setType(iMimeType);
    storageItem->write( 0, item->description().toUtf8() );

    return storageItem;
}

bool NotesBackend::addNote( Buteo::StorageItem& aItem, bool aCommitNow )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QByteArray data;

    if( !aItem.read( 0, aItem.getSize(), data ) ) {
        qCWarning(lcSyncMLPlugin) << "Reading item data failed";
        return false;
    }

    KCalendarCore::Journal::Ptr journal ;
    journal = KCalendarCore::Journal::Ptr( new KCalendarCore::Journal() );

    QString description = QString::fromUtf8( data.constData() );

    journal->setDescription( description );

    // addJournal() takes ownership of journal -> we cannot delete it

    if( !iCalendar->addJournal( journal, iNotebookName ) ) {
        qCWarning(lcSyncMLPlugin) << "Could not add note to calendar";
        journal.clear();
        return false;
    }

    QString id = journal->uid();

    qCDebug(lcSyncMLPlugin) << "New note added, id:" << id;

    aItem.setId( id );

    if( aCommitNow )
    {
        if( !commitChanges() )
        {
            return false;
        }
    }

    return true;

}

bool NotesBackend::modifyNote( Buteo::StorageItem& aItem, bool aCommitNow )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    iStorage->load ( aItem.getId() );
    KCalendarCore::Incidence::Ptr item = iCalendar->incidence( aItem.getId() );

    if( !item ) {
        qCWarning(lcSyncMLPlugin) << "Could not find item to be modified:" << aItem.getId();
        return false;
    }

    QByteArray data;

    if( !aItem.read( 0, aItem.getSize(), data ) ) {
        qCWarning(lcSyncMLPlugin) << "Reading item data failed:" << aItem.getId();
        return false;
    }

    QString description = QString::fromLatin1( data.constData() );

    item->setDescription( description );

    if( aCommitNow )
    {
        if( !commitChanges() )
        {
            return false;
        }
    }

    qCDebug(lcSyncMLPlugin) <<  "Note modified, id:" << aItem.getId();

    return true;
}

bool NotesBackend::deleteNote( const QString& aId, bool aCommitNow )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    iStorage->load ( aId );
    KCalendarCore::Incidence::Ptr journal = iCalendar->incidence( aId );

    if( !journal ) {
        qCWarning(lcSyncMLPlugin) << "Could not find item to be deleted:" << aId;
        return false;
    }

    if( !iCalendar->deleteIncidence( journal ) ) {
        qCWarning(lcSyncMLPlugin) << "Could not delete note:" << aId;
        return false;
    }

    if( aCommitNow )
    {
        if( !commitChanges() )
        {
            return false;
        }
    }

    return true;
}


bool NotesBackend::commitChanges()
{
    bool saved = false;

    if( iStorage && iStorage->save() )
    {
        saved = true;
    }
    else
    {
        qCCritical(lcSyncMLPlugin) << "Couldn't save to storage";
    }
    return saved;
}

void NotesBackend::retrieveNoteItems( KCalendarCore::Incidence::List& aIncidences, QList<Buteo::StorageItem*>& aItems )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    filterIncidences( aIncidences );

    for( int i = 0; i < aIncidences.count(); ++i ) {
        Buteo::StorageItem* item = newItem();
        item->setId( aIncidences[i]->uid() );
        item->setType(iMimeType);
        item->write( 0, aIncidences[i]->description().toUtf8() );
        aItems.append( item );
    }

}

void NotesBackend::retrieveNoteIds( KCalendarCore::Incidence::List& aIncidences, QList<QString>& aIds )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    filterIncidences( aIncidences );

    for( int i = 0; i < aIncidences.count(); ++i ) {
        aIds.append( aIncidences[i]->uid() );
    }

}

void NotesBackend::filterIncidences( KCalendarCore::Incidence::List& aIncidences )
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);

    QString journal( INCIDENCE_TYPE_JOURNAL );

    int i = 0;
    while( i < aIncidences.count() ) {
        KCalendarCore::Incidence::Ptr incidence = aIncidences[i];

        if( incidence->type() != KCalendarCore::Incidence::TypeJournal ) {
            aIncidences.remove( i, 1 );
            incidence.clear();
        }
        else {
            ++i;
        }
    }

}
