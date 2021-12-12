/*
 * This file is part of buteo-sync-plugins package
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2013 - 2021 Jolla Ltd.
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
#include "ContactsChangeNotifierPlugin.h"
#include "ContactsChangeNotifier.h"
#include "SyncMLPluginLogging.h"
#include <QTimer>

using namespace Buteo;

Buteo::StorageChangeNotifierPlugin* ContactsChangeNotifierPluginLoader::createPlugin(const QString& aStorageName)
{
    return new ContactsChangeNotifierPlugin(aStorageName);
}


ContactsChangeNotifierPlugin::ContactsChangeNotifierPlugin(const QString& aStorageName) :
StorageChangeNotifierPlugin(aStorageName),
ihasChanges(false),
iDisableLater(false)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    icontactsChangeNotifier = new ContactsChangeNotifier;
    QObject::connect(icontactsChangeNotifier, SIGNAL(change()),
                     this, SLOT(onChange()));
}

ContactsChangeNotifierPlugin::~ContactsChangeNotifierPlugin()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    delete icontactsChangeNotifier;
}

QString ContactsChangeNotifierPlugin::name() const
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    return iStorageName;
}

bool ContactsChangeNotifierPlugin::hasChanges() const
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    return ihasChanges;
}

void ContactsChangeNotifierPlugin::changesReceived()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    ihasChanges = false;
}

void ContactsChangeNotifierPlugin::onChange()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    qCDebug(lcSyncMLPlugin) << "Change in contacts detected";
    ihasChanges = true;
    if(iDisableLater)
    {
        icontactsChangeNotifier->disable();
    }
    else
    {
        emit storageChange();
    }
}

void ContactsChangeNotifierPlugin::enable()
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    icontactsChangeNotifier->enable();
    iDisableLater = false;
}

void ContactsChangeNotifierPlugin::disable(bool disableAfterNextChange)
{
    FUNCTION_CALL_TRACE(lcSyncMLPluginTrace);
    if(disableAfterNextChange)
    {
        iDisableLater = true;
    }
    else
    {
        icontactsChangeNotifier->disable();
    }
}
