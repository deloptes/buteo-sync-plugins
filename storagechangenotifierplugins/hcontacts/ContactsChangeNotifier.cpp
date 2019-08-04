#include "ContactsChangeNotifier.h"
#include "LogMacros.h"
#include <QList>

const QString DEFAULT_CONTACTS_MANAGER("tracker");

ContactsChangeNotifier::ContactsChangeNotifier() :
iDisabled(true)
{
    FUNCTION_CALL_TRACE;
    iManager = new QContactManager("org.nemomobile.contacts.sqlite");
}

ContactsChangeNotifier::~ContactsChangeNotifier()
{
    disable();
    delete iManager;
}

void ContactsChangeNotifier::enable()
{
    if(iManager && iDisabled)
    {
        QObject::connect(iManager, SIGNAL(contactsAdded(const QList<QContactId>&)),
                         this, SLOT(onContactsAdded(const QList<QContactId>&)));

        QObject::connect(iManager, SIGNAL(contactsRemoved(const QList<QContactId>&)),
                         this, SLOT(onContactsRemoved(const QList<QContactId>&)));

        QObject::connect(iManager, SIGNAL(contactsChanged(const QList<QContactId>&)),
                         this, SLOT(onContactsChanged(const QList<QContactId>&)));
        iDisabled = false;
    }
}

void ContactsChangeNotifier::onContactsAdded(const QList<QContactId>& ids)
{
    FUNCTION_CALL_TRACE;
    if(ids.count())
    {
        QList<QContact> contacts = iManager->contacts(ids);
        // @todo update to print if debugging is enabled
#if 0
        foreach(QContact contact, contacts)
        {
            LOG_DEBUG("Added contact" << contact.displayLabel());
        }
#endif
        emit change();
    }
}

void ContactsChangeNotifier::onContactsRemoved(const QList<QContactId>& ids)
{
    FUNCTION_CALL_TRACE;
    if(ids.count())
    {
        foreach(QContactId id, ids)
        {
            LOG_DEBUG("Removed contact with id" << id);
        }
        emit change();
    }
}

void ContactsChangeNotifier::onContactsChanged(const QList<QContactId>& ids)
{
    FUNCTION_CALL_TRACE;
    if(ids.count())
    {
        QList<QContact> contacts = iManager->contacts(ids);
        emit change();
    }
}

void ContactsChangeNotifier::disable()
{
    FUNCTION_CALL_TRACE;
    iDisabled = true;
    QObject::disconnect(iManager, 0, this, 0);
}
