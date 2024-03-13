#ifndef __PROJECTS_FIX_AFX_H_
#define __PROJECTS_FIX_AFX_H_
#include <stl.h>
#include <quickfix.h>
using namespace fix;
#include <quickfix/config.h>
#include <quickfix/Session.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Values.h>
#include <quickfix/Utility.h>
#include <quickfix/Mutex.h>

#include <quickfix/Except.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/Values.h>
#include <quickfix/Utility.h>
#include <quickfix/Mutex.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#if HAVE_SSL
#include <quickfix/ThreadedSSLSocketAcceptor.h>
#include <quickfix/ThreadedSSLSocketInitiator.h>
#include <quickfix/ThreadedSSLSocketConnection.h>
#include <quickfix/SSLSocketAcceptor.h>
#include <quickfix/SSLSocketInitiator.h>
#include <quickfix/SSLSocketConnection.h>
#endif /// HAVE_SSL
#include <quickfix/ThreadedSocketInitiator.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/SocketAcceptor.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/HttpServer.h>
#include <quickfix/HttpConnection.h>
#include <quickfix/HttpMessage.h>
#include <quickfix/HttpParser.h>

#include <quickfix/fix40/NewOrderSingle.h>
#include <quickfix/fix41/NewOrderSingle.h>
#include <quickfix/fix42/NewOrderSingle.h>
#include <quickfix/fix43/NewOrderSingle.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <quickfix/fix50/NewOrderSingle.h>

#include <quickfix/fix40/ExecutionReport.h>
#include <quickfix/fix41/ExecutionReport.h>
#include <quickfix/fix42/ExecutionReport.h>
#include <quickfix/fix43/ExecutionReport.h>
#include <quickfix/fix44/ExecutionReport.h>
#include <quickfix/fix50/ExecutionReport.h>

#include "sessionsettings.h"
#include "application.h"
#include "fix.h"
#endif ///__PROJECTS_FIX_AFX_H_