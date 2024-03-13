#ifndef __PROJECTS_FIX_APPLICATION_H_
#define __PROJECTS_FIX_APPLICATION_H_

class FixApplication final : public FIX::Application,
                             public FIX::MessageCracker {
public:
  FixApplication() : m_orderID(0), m_execID(0) {
  }

  // Application overloads
  void onCreate(const FIX::SessionID &);
  void onLogon(const FIX::SessionID &sessionID);
  void onLogout(const FIX::SessionID &sessionID);
  void toAdmin(FIX::Message &, const FIX::SessionID &);
  void toApp(FIX::Message &, const FIX::SessionID &) EXCEPT(FIX::DoNotSend);
  void fromAdmin(const FIX::Message &, const FIX::SessionID &)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat,
             FIX::IncorrectTagValue, FIX::RejectLogon);
  void fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat,
             FIX::IncorrectTagValue, FIX::UnsupportedMessageType);

  // MessageCracker overloads
  void onMessage(const FIX40::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX41::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX42::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX43::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX44::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX50::NewOrderSingle &, const FIX::SessionID &);

  std::string genOrderID() {
    return std::to_string(++m_orderID);
  }
  std::string genExecID() {
    return std::to_string(++m_execID);
  }

private:
  int m_orderID, m_execID;
};

#endif ///__PROJECTS_FIX_APPLICATION_H_
