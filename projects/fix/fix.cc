#include "stdafx.h"

const std::string fix_config = R"(
[DEFAULT]
ConnectionType=initiator
ReconnectInterval=10
LogonDay=Mon.
LogoutDay=Sun.
StartDay=Mon.
EndDay=Sun.
LogonTime=00:00:00
LogoutTime=23:59:58
StartTime=00:00:00
EndTime=23:59:59
HeartBtInt=60
TimeZone=Europe/London
MillisecondsInTimeStamp=Y
SSLProtocol=all
SendResetSeqNumFlag=Y
FileLogPath=\caches\lmax.marketdata\log
FileStorePath=\caches\lmax.marketdata\store
[SESSION]
TargetCompID=LMXBDM
#Account=705377715
#Username=developer
#SenderCompID=developer
#Password=pa$$w0rd
Account=705377715
Username=Martell2020
SenderCompID=Martell2020
Password=pa$$w0rd		
SocketConnectHost=fix-marketdata.london-demo.lmax.com
BeginString=FIX.4.4
SocketConnectPort=443
ScreenLogShowIncoming=N
ScreenLogShowOutgoing=N
ScreenLogShowEvents=N
EnableProtocolFileLog=N
  )";

const std::string config_demo = R"(
  [DEFAULT]
ConnectionType=acceptor
SocketAcceptPort=5001
SocketReuseAddress=Y
StartTime=00:00:00
EndTime=00:00:00
FileLogPath=log
ServerCertificateFile=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/cfg/certs/127_0_0_1_server.crt
ServerCertificateKeyFile=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/cfg/certs/127_0_0_1_server.key
SSLProtocol = all
TimestampPrecision=6
PreserveMessageFieldsOrder=N

[SESSION]
BeginString=FIX.4.0
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX40.xml

[SESSION]
BeginString=FIX.4.0
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX40.xml

[SESSION]
BeginString=FIX.4.1
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX41.xml

[SESSION]
BeginString=FIX.4.1
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX41.xml

[SESSION]
BeginString=FIX.4.2
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX42.xml

[SESSION]
BeginString=FIX.4.2
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX42.xml

[SESSION]
BeginString=FIX.4.3
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX43.xml

[SESSION]
BeginString=FIX.4.3
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX43.xml

[SESSION]
BeginString=FIX.4.4
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX44.xml

[SESSION]
BeginString=FIX.4.4
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
DataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX44.xml

[SESSION]
BeginString=FIXT.1.1
DefaultApplVerID=FIX.5.0
SenderCompID=EXECUTOR
TargetCompID=CLIENT1
FileStorePath=store
TransportDataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIXT11.xml
AppDataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX50.xml

[SESSION]
BeginString=FIXT.1.1
DefaultApplVerID=FIX.5.0
SenderCompID=EXECUTOR
TargetCompID=CLIENT2
FileStorePath=store
TransportDataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIXT11.xml
AppDataDictionary=C:/Users/k34ub/source/skstu/devkit/build/3rdparty/quickfix/config/spec/FIX50.xml
)";
Fix::Fix() {
}
Fix::~Fix() {
}
void Fix::Release() const {
  delete this;
}
bool Fix::Ready() const {
  return false;
}
std::unique_ptr<FIX::Acceptor> acceptor;
bool Fix::Start() {
  do {
    if (open_.load())
      break;

    std::stringstream gangplank;
    std::iostream i_setting(gangplank.rdbuf());
    i_setting.write(config_demo.data(), config_demo.size());

    FIX::SessionSettings settings(i_setting);
    try {
      // i_setting >> settings;
      // result = true;
    } catch (FIX::ConfigError &e) {
      auto what = e.what();
    }

    Application application;
    FIX::FileStoreFactory storeFactory(settings);
    FIX::ScreenLogFactory logFactory(settings);

#if HAVE_SSL
    /*
        if (isSSL.compare("SSL") == 0)
          acceptor =
              std::unique_ptr<FIX::Acceptor>(new FIX::ThreadedSSLSocketAcceptor(
                  application, storeFactory, settings, logFactory));
        else if (isSSL.compare("SSL-ST") == 0)
          acceptor = std::unique_ptr<FIX::Acceptor>(new FIX::SSLSocketAcceptor(
              application, storeFactory, settings, logFactory));
        else
    #else
        acceptor = std::unique_ptr<FIX::Acceptor>(new FIX::SocketAcceptor(
            application, storeFactory, settings, logFactory));
    #endif
    */
    acceptor =
        std::unique_ptr<FIX::Acceptor>(new FIX::ThreadedSSLSocketAcceptor(
            application, storeFactory, settings, logFactory));
#else
    acceptor = std::unique_ptr<FIX::Acceptor>(new FIX::SocketAcceptor(
        application, storeFactory, settings, logFactory));
#endif
    acceptor->start();
    // wait();
    // acceptor->stop();

    open_.store(true);
  } while (0);
  return open_.load();
}
void Fix::Stop() {
  do {
    if (!open_.load())
      break;

    open_.store(false);
  } while (0);
}

Fix *__gpFix = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpFix) {
      break;
    }
    __gpFix = new Fix();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IFix *>(__gpFix));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpFix) {
      break;
    }
    delete __gpFix;
    __gpFix = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif