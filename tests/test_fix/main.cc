#include "stdafx.h"
#include <quickfix.h>

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

fix::IFix *__gpFix = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpFix = fix::IFix::Create("fix.dll");
#else
  __gpFix = fix::IFix::Create("fix.so");
#endif
  if (__gpFix) {
    __gpFix->Start(fix_config.c_str());
  }
  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      if (__gpFix) {
        __gpFix->Stop();
      }
      break;
    }
  } while (1);
  fix::IFix::Destroy(&__gpFix);
  return 0;
}