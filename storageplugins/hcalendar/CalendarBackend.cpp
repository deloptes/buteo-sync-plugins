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

#include "CalendarBackend.h"
#include <KCalendarCore/Event>
#include <KCalendarCore/Journal>
#include <LogMacros.h>
#include <QDir>
#include <QDebug>

CalendarBackend::CalendarBackend() : iCalendar( 0 ), iStorage( 0 )
{
	FUNCTION_CALL_TRACE;
}

CalendarBackend::~CalendarBackend()
{
	FUNCTION_CALL_TRACE;
}

namespace {
    mKCal::Notebook::Ptr defaultLocalCalendarNotebook(mKCal::ExtendedStorage::Ptr storage)
    {
        mKCal::Notebook::List notebooks = storage->notebooks();
        Q_FOREACH (const mKCal::Notebook::Ptr nb, notebooks) {
            if (nb->isMaster() && !nb->isShared() && nb->pluginName().isEmpty()) {
                // assume that this is the default local calendar notebook.
                return nb;
            }
        }
        LOG_WARNING("No default local calendar notebook found!");
        return mKCal::Notebook::Ptr();
    }
}

bool CalendarBackend::init(const QString& aUid)
{
    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Creating Default Maemo Storage");
    iNotebookStr = aNotebookName;

    iCalendar = mKCal::ExtendedCalendar::Ptr( new mKCal::ExtendedCalendar( QTimeZone::systemTimeZone()) );
    iCalendar = mKCal::ExtendedCalendar::Ptr( new mKCal::ExtendedCalendar( KDateTime::Spec::LocalZone( ) ));
    iStorage = iCalendar->defaultStorage( iCalendar );
    if (!iStorage->open()) {
        LOG_WARNING("Calendar storage open failed");
        iStorage.clear();
        iCalendar.clear();
        return false;
    }

    // If we have an Uid, we try to get the corresponding Notebook
    mKCal::Notebook::Ptr openedNb;
    if (!aUid.isEmpty()) {
        openedNb = iStorage->notebook(aUid);
        if (!openedNb) {
            LOG_WARNING("Invalid notebook UID specified:" << aUid << "- aborting sync");
            iStorage.clear();
            iCalendar.clear();
            return false;
        }
    } else {
        // otherwise, open the default local notebook.
        // Note: this is NOT the iStorage->defaultNotebook()
        // TODO: find out why we don't use the defaultNotebook here!
        openedNb = defaultLocalCalendarNotebook(iStorage);
        if (!openedNb) {
            LOG_WARNING("Unable to open the default local notebook for sync - aborting");
            iStorage.clear();
            iCalendar.clear();
            return false;
        }
    }

    if (!iStorage->loadNotebookIncidences(openedNb->uid())) {
        LOG_WARNING("Not able to initialize calendar");
        iStorage.clear();
        iCalendar.clear();
        return false;
    }

    iNotebookStr = openedNb->uid();
    LOG_DEBUG("Calendar initialized, opened notebook:" << openedNb->uid() << openedNb->name());
    return true;
}

bool CalendarBackend::uninit()
{
    FUNCTION_CALL_TRACE;

    if( iStorage ) {
        LOG_TRACE("Closing calendar storage...");
        iStorage->close();
        LOG_TRACE("Done");
        iStorage.clear();
    }

    if( iCalendar ) {
        LOG_TRACE("Closing calendar...");
        iCalendar->close();
        LOG_TRACE("Done");
        iCalendar.clear();
    }

    return true;
}

bool CalendarBackend::getAllIncidences( KCalendarCore::Incidence::List& aIncidences )
{
	FUNCTION_CALL_TRACE;

	if( !iStorage ) {
	    return false;
	}

	if( !iStorage->allIncidences( &aIncidences, iNotebookStr ) ) {
        LOG_WARNING("Error Retrieving ALL Incidences from the  Storage ");
        return false;
	}

    filterIncidences( aIncidences );
    return true;
}

void CalendarBackend::filterIncidences(KCalendarCore::Incidence::List& aList)
{
	FUNCTION_CALL_TRACE;
	QString event(INCIDENCE_TYPE_EVENT);
	QString todo(INCIDENCE_TYPE_TODO);

	for (int i = 0; i < aList.size(); ++i) {
        KCalendarCore::Incidence::Ptr incidence = aList.at(i);
        if ((incidence->type() != KCalendarCore::Incidence::TypeEvent) && (incidence->type() != KCalendarCore::Incidence::TypeTodo)) {
	        LOG_DEBUG("Removing incidence type" << incidence->typeStr());
                aList.remove( i, 1);
		incidence.clear();
	    }
        }
}

bool CalendarBackend::getAllNew( KCalendarCore::Incidence::List& aIncidences, const QDateTime& aTime )
{
    FUNCTION_CALL_TRACE;

    if( !iStorage ) {
        return false;
    }

    if( !iStorage->insertedIncidences( &aIncidences, aTime, iNotebookStr) ) {
        LOG_WARNING("Error Retrieving New Incidences from the Storage" );
        return false;
    }

    filterIncidences( aIncidences );
    return true;
}

bool CalendarBackend::getAllModified( KCalendarCore::Incidence::List& aIncidences, const QDateTime& aTime )
{
	FUNCTION_CALL_TRACE;

    if( !iStorage ) {
        return false;
    }

    if( !iStorage->modifiedIncidences( &aIncidences, aTime, iNotebookStr ) ) {
        LOG_WARNING(" Error retrieving modified Incidences ");
        return false;
    }

    filterIncidences( aIncidences );
    return true;
}

bool CalendarBackend::getAllDeleted( KCalendarCore::Incidence::List& aIncidences, const QDateTime& aTime )
{
	FUNCTION_CALL_TRACE;

    if( !iStorage ) {
        return false;
    }

    if( !iStorage->deletedIncidences( &aIncidences, aTime, iNotebookStr ) ) {
        LOG_WARNING(" Error retrieving deleted Incidences ");
        return false;
    }

    filterIncidences( aIncidences );
    return true;
}

KCalendarCore::Incidence::Ptr CalendarBackend::getIncidence( const QString& aUID )
{
    FUNCTION_CALL_TRACE;

    QStringList iDs = aUID.split(ID_SEPARATOR);
    KCalendarCore::Incidence::Ptr incidence;
    if (iDs.size() == 2) {
       iStorage->load ( iDs.at(0), QDateTime::fromString(iDs.at(1), Qt::ISODate) );
       incidence = iCalendar->incidence(iDs.at(0), QDateTime::fromString(iDs.at(1), Qt::ISODate));
    } else {
	   iStorage->load ( aUID );
	   incidence = iCalendar->incidence( aUID );
    }
    return incidence;
}

QString CalendarBackend::getVCalString(KCalendarCore::Incidence::Ptr aInci)
{
    FUNCTION_CALL_TRACE;

    Q_ASSERT( aInci );

    QString vcal;

    KCalendarCore::Incidence::Ptr temp = KCalendarCore::Incidence::Ptr ( aInci->clone() );
    if(temp) {
    KCalendarCore::Calendar::Ptr tempCalendar( new KCalendarCore::MemoryCalendar( QTimeZone::utc() ) );
	tempCalendar->addIncidence(temp);
        KCalendarCore::VCalFormat vcf;
        vcal = vcf.toString(tempCalendar);
    }
    else {
    	LOG_WARNING("Error Cloning the Incidence for VCal String");
    }

    return vcal;
}

QString CalendarBackend::getICalString(KCalendarCore::Incidence::Ptr aInci)
{
    FUNCTION_CALL_TRACE;

    Q_ASSERT( aInci );

    KCalendarCore::Incidence::Ptr temp = KCalendarCore::Incidence::Ptr ( aInci->clone() );

    QString ical;
    if( temp ) {
    KCalendarCore::Calendar::Ptr tempCalendar( new KCalendarCore::MemoryCalendar( QTimeZone::utc() ) );
	tempCalendar->addIncidence(temp);
    KCalendarCore::ICalFormat icf;
	ical = icf.toString(tempCalendar);
    }
    else {
    	LOG_WARNING("Error Cloning the Incidence for Ical String");
    }

    return ical;
}

KCalendarCore::Incidence::Ptr CalendarBackend::getIncidenceFromVcal( const QString& aVString )
{
    FUNCTION_CALL_TRACE;

    KCalendarCore::Incidence::Ptr pInci;

    KCalendarCore::Calendar::Ptr tempCalendar( new KCalendarCore::MemoryCalendar( QTimeZone::systemTimeZone()) );
    KCalendarCore::VCalFormat vcf;
    vcf.fromString(tempCalendar, aVString);
    KCalendarCore::Incidence::List lst = tempCalendar->rawIncidences();

    if(!lst.isEmpty()) {
        pInci = KCalendarCore::Incidence::Ptr ( lst[0]->clone() );
    }
    else {
        LOG_WARNING("VCal to Incidence Conversion Failed ");
    }
    return pInci;
}

KCalendarCore::Incidence::Ptr CalendarBackend::getIncidenceFromIcal( const QString& aIString )
{
	FUNCTION_CALL_TRACE;

    KCalendarCore::Incidence::Ptr pInci;

    KCalendarCore::Calendar::Ptr tempCalendar( new KCalendarCore::MemoryCalendar( QTimeZone::systemTimeZone()) );
    KCalendarCore::ICalFormat icf;
    icf.fromString(tempCalendar, aIString);
    KCalendarCore::Incidence::List lst = tempCalendar->rawIncidences();

    if(!lst.isEmpty()) {
        pInci = KCalendarCore::Incidence::Ptr ( lst[0]->clone() );
    } else {
    	LOG_WARNING("ICal to Incidence Conversion Failed ");
    }

    return pInci;
}

bool CalendarBackend::addIncidence( KCalendarCore::Incidence::Ptr aInci, bool commitNow )
{
    FUNCTION_CALL_TRACE;

    if( !iCalendar || !iStorage ) {
        return false;
    }

    switch(aInci->type())
    {
        case KCalendarCore::Incidence::TypeEvent:
            {
                KCalendarCore::Event::Ptr event = aInci.staticCast<KCalendarCore::Event>();
                if(!iCalendar->addEvent(event, iNotebookStr))
                {
                    LOG_WARNING("Could not add event");
                    return false;
                }
            }
            break;
        case KCalendarCore::Incidence::TypeTodo:
            {
                KCalendarCore::Todo::Ptr todo = aInci.staticCast<KCalendarCore::Todo>();
                if(!iCalendar->addTodo(todo, iNotebookStr))
                {
                    LOG_WARNING("Could not add todo");
                    return false;
                }
            }
            break;
        default:
            LOG_WARNING("Could not add incidence, wrong type" << aInci->type());
            return false;
    }
    
    // if you add an incidence, that incidence will be owned by calendar
    // so no need to delete it. Committing for each modification, can cause performance
    // problems.

    if( commitNow )  {
        if( !iStorage->save() )
        {
            LOG_WARNING( "Could not commit changes to calendar");
            return false;
        }
        LOG_DEBUG( "Single incidence committed");
    }

    LOG_DEBUG("Added an item with UID : " << aInci->uid() << "Recurrence Id :" << aInci->recurrenceId().toString());

    return true;
}

bool CalendarBackend::commitChanges()
{
    FUNCTION_CALL_TRACE;
    bool changesCommitted = false;

    if( !iStorage )
    {
        LOG_WARNING("No calendar storage!");
    }
    else if( iStorage->save() )  {
        LOG_DEBUG( "Committed changes to calendar");
        changesCommitted = true;
    }
    else
    {
        LOG_DEBUG( "Could not commit changes to calendar");
    }
    return changesCommitted;
}

bool CalendarBackend::modifyIncidence( KCalendarCore::Incidence::Ptr aInci, const QString& aUID, bool commitNow )
{
	FUNCTION_CALL_TRACE;

    if( !iCalendar || !iStorage ) {
        return false;
    }

    KCalendarCore::Incidence::Ptr origInci = getIncidence ( aUID );

    if( !origInci ) {
        LOG_WARNING("Item with UID" << aUID << "does not exist. Cannot modify");
        return false;
    }

    if( !modifyIncidence( origInci, aInci ) ) {
        LOG_WARNING( "Could not make modifications to incidence" );
        return false;
    }

    if( commitNow )  {
        if( !iStorage->save() )
        {
            LOG_WARNING( "Could not commit changes to calendar");
            return false;
        }
        LOG_DEBUG( "Single incidence committed");
    }

    return true;
}

CalendarBackend::ErrorStatus CalendarBackend::deleteIncidence( const QString& aUID )
{
	FUNCTION_CALL_TRACE;
    CalendarBackend::ErrorStatus errorCode = CalendarBackend::STATUS_OK;

    if( !iCalendar || !iStorage ) {
        errorCode = CalendarBackend::STATUS_GENERIC_ERROR;
    }

    KCalendarCore::Incidence::Ptr incidence = getIncidence( aUID );
    
    if( !incidence ) {
        LOG_WARNING( "Could not find incidence to delete with UID" << aUID );
        errorCode = CalendarBackend::STATUS_ITEM_NOT_FOUND;
    }

    if( !iCalendar->deleteIncidence( incidence) )
    {
        LOG_WARNING( "Could not delete incidence with UID" << aUID );
        errorCode = CalendarBackend::STATUS_GENERIC_ERROR;
    }

    if( !iStorage->save() ) {
        LOG_WARNING( "Could not commit changes to calendar");
        errorCode =  CalendarBackend::STATUS_GENERIC_ERROR;
    }

    return errorCode;
}

bool CalendarBackend::modifyIncidence( KCalendarCore::Incidence::Ptr aIncidence, KCalendarCore::Incidence::Ptr aIncidenceData )
{
    FUNCTION_CALL_TRACE;

    Q_ASSERT( aIncidence );
    Q_ASSERT( aIncidenceData );

    // Save critical data from original item
    aIncidenceData->setUid( aIncidence->uid() );
    aIncidenceData->setCreated( aIncidence->created() );

    if( aIncidence->type() != aIncidenceData->type() ) {
        LOG_WARNING( "Expected incidence type" << aIncidence->typeStr() <<", got" << aIncidenceData->typeStr() );
        return false;
    }

    if( aIncidence->type() == KCalendarCore::Incidence::TypeEvent || aIncidence->type() == KCalendarCore::Incidence::TypeTodo )
    {
    KCalendarCore::IncidenceBase::Ptr inc = aIncidence;
    KCalendarCore::IncidenceBase::Ptr data = aIncidenceData;
        *inc = *data;    
    }
    else {
        LOG_WARNING( "Unsupported incidence type:" << aIncidence->typeStr() );
        return false;
    }

    iCalendar->setNotebook( aIncidence, iNotebookStr );

    return true;

}
