// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2017-2022 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "zmqnotificationinterface.h"
#include "zmqpublishnotifier.h"

#include "version.h"
#include "main.h"
#include "streams.h"
#include "util/system.h"

void zmqError(const char *str)
{
    LogPrint("zmq", "zmq: Error: %s, errno=%s\n", str, zmq_strerror(errno));
}

CZMQNotificationInterface::CZMQNotificationInterface() : pcontext(NULL)
{
}

CZMQNotificationInterface::~CZMQNotificationInterface()
{
    Shutdown();

    for (std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin(); i!=notifiers.end(); ++i)
    {
        delete *i;
    }
}

CZMQNotificationInterface* CZMQNotificationInterface::CreateWithArguments(const std::map<std::string, std::string> &args)
{
    CZMQNotificationInterface* notificationInterface = NULL;
    std::map<std::string, CZMQNotifierFactory> factories;
    std::list<CZMQAbstractNotifier*> notifiers;

    factories["pubhashblock"] = CZMQAbstractNotifier::Create<CZMQPublishHashBlockNotifier>;
    factories["pubhashtx"] = CZMQAbstractNotifier::Create<CZMQPublishHashTransactionNotifier>;
    factories["pubrawblock"] = CZMQAbstractNotifier::Create<CZMQPublishRawBlockNotifier>;
    factories["pubrawtx"] = CZMQAbstractNotifier::Create<CZMQPublishRawTransactionNotifier>;
    factories["pubcheckedblock"] = CZMQAbstractNotifier::Create<CZMQPublishCheckedBlockNotifier>;

    for (std::map<std::string, CZMQNotifierFactory>::const_iterator i=factories.begin(); i!=factories.end(); ++i)
    {
        std::map<std::string, std::string>::const_iterator j = args.find("-zmq" + i->first);
        if (j!=args.end())
        {
            CZMQNotifierFactory factory = i->second;
            std::string address = j->second;
            CZMQAbstractNotifier *notifier = factory();
            notifier->SetType(i->first);
            notifier->SetAddress(address);
            notifiers.push_back(notifier);
        }
    }

    if (!notifiers.empty())
    {
        notificationInterface = new CZMQNotificationInterface();
        notificationInterface->notifiers = notifiers;

        if (!notificationInterface->Initialize())
        {
            delete notificationInterface;
            notificationInterface = NULL;
        }
    }

    return notificationInterface;
}

// Called at startup to conditionally set up ZMQ socket(s)
bool CZMQNotificationInterface::Initialize()
{
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    LogPrint("zmq", "zmq: version %d.%d.%d\n", major, minor, patch);

    LogPrint("zmq", "zmq: Initialize notification interface\n");
    assert(!pcontext);

    pcontext = zmq_ctx_new();

    if (!pcontext)
    {
        zmqError("Unable to initialize context");
        return false;
    }

    std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin();
    for (; i!=notifiers.end(); ++i)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->Initialize(pcontext))
        {
            LogPrint("zmq", "  Notifier %s ready (address = %s)\n", notifier->GetType(), notifier->GetAddress());
        }
        else
        {
            LogPrint("zmq", "  Notifier %s failed (address = %s)\n", notifier->GetType(), notifier->GetAddress());
            break;
        }
    }

    if (i!=notifiers.end())
    {
        return false;
    }

    return true;
}

// Called during shutdown sequence
void CZMQNotificationInterface::Shutdown()
{
    LogPrint("zmq", "zmq: Shutdown notification interface\n");
    if (pcontext)
    {
        for (std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin(); i!=notifiers.end(); ++i)
        {
            CZMQAbstractNotifier *notifier = *i;
            LogPrint("zmq", "   Shutdown notifier %s at %s\n", notifier->GetType(), notifier->GetAddress());
            notifier->Shutdown();
        }
        zmq_ctx_term(pcontext);

        pcontext = 0;
    }
}

void CZMQNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindex)
{
    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyBlock(pindex))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::BlockChecked(const CBlock& block, const CValidationState& state)
{
    if (state.IsInvalid()) {
        return;
    }

    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyBlock(block))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlock *pblock, const int nHeight)
{
    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTransaction(tx))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}
