#include "stdafx.h"

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

void FixApplication::onCreate(const FIX::SessionID &sessionID) {
}
void FixApplication::onLogon(const FIX::SessionID &sessionID) {
}
void FixApplication::onLogout(const FIX::SessionID &sessionID) {
}
void FixApplication::toAdmin(FIX::Message &message,
                             const FIX::SessionID &sessionID) {
}
void FixApplication::toApp(FIX::Message &message,
                           const FIX::SessionID &sessionID)
    EXCEPT(FIX::DoNotSend) {
}

void FixApplication::fromAdmin(const FIX::Message &message,
                               const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue,
           FIX::RejectLogon) {
}

void FixApplication::fromApp(const FIX::Message &message,
                             const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue,
           FIX::UnsupportedMessageType) {
  crack(message, sessionID);
}

void FixApplication::onMessage(const FIX40::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX40::ExecutionReport executionReport = FIX40::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),
      FIX::OrdStatus(FIX::OrdStatus_FILLED), symbol, side, orderQty,
      FIX::LastShares(orderQty), FIX::LastPx(price), FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  executionReport.set(clOrdID);

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}

void FixApplication::onMessage(const FIX41::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX41::ExecutionReport executionReport = FIX41::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),
      FIX::ExecType(FIX::ExecType_FILL), FIX::OrdStatus(FIX::OrdStatus_FILLED),
      symbol, side, orderQty, FIX::LastShares(orderQty), FIX::LastPx(price),
      FIX::LeavesQty(0), FIX::CumQty(orderQty), FIX::AvgPx(price));

  executionReport.set(clOrdID);

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}

void FixApplication::onMessage(const FIX42::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX42::ExecutionReport executionReport = FIX42::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),
      FIX::ExecType(FIX::ExecType_FILL), FIX::OrdStatus(FIX::OrdStatus_FILLED),
      symbol, side, FIX::LeavesQty(0), FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  executionReport.set(clOrdID);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastShares(orderQty));
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}

void FixApplication::onMessage(const FIX43::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX43::ExecutionReport executionReport = FIX43::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_FILL), FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side, FIX::LeavesQty(0), FIX::CumQty(orderQty), FIX::AvgPx(price));

  executionReport.set(clOrdID);
  executionReport.set(symbol);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}

void FixApplication::onMessage(const FIX44::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX44::ExecutionReport executionReport = FIX44::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_TRADE), FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side, FIX::LeavesQty(0), FIX::CumQty(orderQty), FIX::AvgPx(price));

  executionReport.set(clOrdID);
  executionReport.set(symbol);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}

void FixApplication::onMessage(const FIX50::NewOrderSingle &message,
                               const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT)
    throw FIX::IncorrectTagValue(ordType.getTag());

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  FIX50::ExecutionReport executionReport = FIX50::ExecutionReport(
      FIX::OrderID(genOrderID()), FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_TRADE), FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side, FIX::LeavesQty(0), FIX::CumQty(orderQty));

  executionReport.set(clOrdID);
  executionReport.set(symbol);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));
  executionReport.set(FIX::LastPx(price));
  executionReport.set(FIX::AvgPx(price));

  if (message.isSet(account))
    executionReport.setField(message.get(account));

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {
  }
}
