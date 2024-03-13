#include "stdafx.h"

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
#if 0
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
#endif
bool Fix::Start(const char *settings) {
  do {
    if (open_.load())
      break;
    if (!settings)
      break;
    settings_ = settings;

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