#include "stdafx.h"

string String::LowerA(const string &input) {
  string result(input);
  if (!result.empty())
    transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
wstring String::LowerW(const wstring &input) {
  wstring result(input);
  if (!result.empty())
    transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

string String::UpperA(const string &input) {
  string result(input);
  if (!result.empty()) {
    transform(result.begin(), result.end(), result.begin(), ::toupper);
  }
  return result;
}

wstring String::UpperW(const wstring &input) {
  wstring result(input);
  if (!result.empty()) {
    transform(result.begin(), result.end(), result.begin(), ::toupper);
  }
  return result;
}

string String::BinaryToHexString(const string &s) {
  ostringstream oss;
  for (unsigned char ch : s) {
    oss << hex << setw(2) << setfill('0') << (int)ch;
  }
  return oss.str();
}

string String::HexStringToBinary(const string &s) {
  string result;
  do {
    if ((s.length() % 2) != 0) {
      break;
    }
    result.reserve(s.length() / 2);
    string extract;
    for (string::const_iterator pos = s.begin(); pos < s.end(); pos += 2) {
      extract.assign(pos, pos + 2);
      result.push_back(stoi(extract, nullptr, 16));
    }
  } while (0);
  return result;
}

vector<string> String::StringSpilt(const string &input, const string &delim) {
  vector<string> result;
  do {
    if (input.empty())
      break;
    if (delim.empty()) {
      result.emplace_back(input);
      break;
    }
    regex re(delim);
    result = vector<string>{
        sregex_token_iterator(input.begin(), input.end(), re, -1),
        sregex_token_iterator()};
  } while (0);
  return result;
}
vector<wstring> String::WStringSplit(const wstring &input,
                                     const wstring &delim) {
  vector<wstring> result;
  do {
    if (input.empty())
      break;
    if (delim.empty()) {
      result.emplace_back(input);
      break;
    }
    wregex re(delim);
    result = vector<wstring>{
        wsregex_token_iterator(input.begin(), input.end(), re, -1),
        wsregex_token_iterator()};
  } while (0);
  return result;
}

tuple<string, int> String::UnmadeIPAddr(const string &addr) {
  tuple<string, int> result;
  do {
    if (addr.empty())
      break;
    const regex pattern(
        "^((?:[0-9]{1,3}\\.){3}[0-9]{1,3}|(?:[0-9a-fA-F]{0,4}:){7}[0-9a-fA-F]{"
        "0,4}|(?:[0-9a-fA-F]{0,4}:){6}:[0-9a-fA-F]{0,4}|(?:[0-9a-fA-F]{0,4}:){"
        "5}(?::[0-9a-fA-F]{0,4}){1,2}|(?:[0-9a-fA-F]{0,4}:){4}(?::[0-9a-fA-F]{"
        "0,4}){1,3}|(?:[0-9a-fA-F]{0,4}:){3}(?::[0-9a-fA-F]{0,4}){1,4}|(?:[0-"
        "9a-fA-F]{0,4}:){2}(?::[0-9a-fA-F]{0,4}){1,5}|[0-9a-fA-F]{0,4}:(?::[0-"
        "9a-fA-F]{0,4}){1,6}|:(?::[0-9a-fA-F]{0,4}){1,7}|(?:[0-9]{1,3}\\.){3}["
        "0-9]{1,3}|\\[?(?:::[0-9a-fA-F]{0,4})?\\]?)(?::([0-9]{1,5}))?$");
    smatch matches;
    if (!regex_match(addr, matches, pattern))
      break;
    string strIPPort = matches[0];
    string strIPAddress = matches[1];
    string strPort = matches[2];
    int port = strtol(strPort.c_str(), nullptr, 10);
    if (!strIPAddress.empty() && strIPAddress.front() == '[' &&
        strIPAddress.back() == ']') {
      strIPAddress = strIPAddress.substr(1, strIPAddress.size() - 2);
    }
    result = make_tuple(strIPAddress, port);
  } while (0);
  return result;
}
string String::MadeIPAddr(const string &ip, const int &port) {
  string result;
  if (ip.find('.') != string::npos) {
    result.append(result).append(":").append(to_string(port));
  } else if (ip.find(']') != string::npos) {
    result.append("[").append(ip).append("]:").append(to_string(port));
  }
  return result;
}
string String::UrlFixA(const string &inputUrlOrPath) {
  string result = inputUrlOrPath;
  if (result.empty())
    return result;
  for (auto it = result.begin(); it != result.end(); ++it) {
    if (*it == '\\')
      *it = '/';
  }
  do {
    auto found = result.find("//");
    if (found == string::npos)
      break;
    result.replace(found, string("//").size(), "/");
  } while (1);

  do {
    auto found = result.find(":/");
    if (found == string::npos)
      break;
    result.replace(found, string(":/").size(), "://");
  } while (0);
  return result;
}