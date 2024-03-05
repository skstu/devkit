#include "stdafx.h"

string String::LowerA(const string &input) {
    string result(input);
    if (!result.empty()) {
        transform(result.begin(), result.end(), result.begin(), ::tolower);
    }
    return result;
}
wstring String::LowerW(const wstring &input) {
    wstring result(input);
    if (!result.empty()) {
        transform(result.begin(), result.end(), result.begin(), ::tolower);
    }
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
        if (input.empty()) {
            break;
        }
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
vector<wstring>
String::WStringSplit(const wstring &input, const wstring &delim) {
    vector<wstring> result;
    do {
        if (input.empty()) {
            break;
        }
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
        if (addr.empty()) {
            break;
        }
        const regex pattern(
            "^((?:[0-9]{1,3}\\.){3}[0-9]{1,3}|(?:[0-9a-fA-F]{0,4}:){7}[0-9a-fA-"
            "F]{"
            "0,4}|(?:[0-9a-fA-F]{0,4}:){6}:[0-9a-fA-F]{0,4}|(?:[0-9a-fA-F]{0,4}"
            ":){"
            "5}(?::[0-9a-fA-F]{0,4}){1,2}|(?:[0-9a-fA-F]{0,4}:){4}(?::[0-9a-fA-"
            "F]{"
            "0,4}){1,3}|(?:[0-9a-fA-F]{0,4}:){3}(?::[0-9a-fA-F]{0,4}){1,4}|(?:["
            "0-"
            "9a-fA-F]{0,4}:){2}(?::[0-9a-fA-F]{0,4}){1,5}|[0-9a-fA-F]{0,4}:(?::"
            "[0-"
            "9a-fA-F]{0,4}){1,6}|:(?::[0-9a-fA-F]{0,4}){1,7}|(?:[0-9]{1,3}\\.){"
            "3}["
            "0-9]{1,3}|\\[?(?:::[0-9a-fA-F]{0,4})?\\]?)(?::([0-9]{1,5}))?$");
        smatch matches;
        if (!regex_match(addr, matches, pattern)) {
            break;
        }
        string strIPPort = matches[0];
        string strIPAddress = matches[1];
        string strPort = matches[2];
        int port = strtol(strPort.c_str(), nullptr, 10);
        if (!strIPAddress.empty() && strIPAddress.front() == '['
            && strIPAddress.back() == ']') {
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
    if (result.empty()) {
        return result;
    }
    for (auto it = result.begin(); it != result.end(); ++it) {
        if (*it == '\\') {
            *it = '/';
        }
    }
    do {
        auto found = result.find("//");
        if (found == string::npos) {
            break;
        }
        result.replace(found, string("//").size(), "/");
    } while (1);

    do {
        auto found = result.find(":/");
        if (found == string::npos) {
            break;
        }
        result.replace(found, string(":/").size(), "://");
    } while (0);
    return result;
}

std::string String::WemadeEncode(const std::string &strSrc) {
    std::string result;
    auto nSrcLen = strSrc.length();
    auto pszSrc = (unsigned char *)strSrc.c_str();
    int nDestPos = 0;
    int nRestCount = 0;
    unsigned char chMade = 0, chRest = 0;
    for (decltype(nSrcLen) i = 0; i < nSrcLen; i++) {
        chMade = ((chRest | (pszSrc[i] >> (2 + nRestCount))) & 0x3f);
        chRest = (((pszSrc[i] << (8 - (2 + nRestCount))) >> 2) & 0x3f);
        nRestCount += 2;
        if (nRestCount < 6) {
            result.push_back(chMade + 0x3c);
        } else {
            result.push_back(chMade + 0x3c);
            result.push_back(chRest + 0x3c);

            nRestCount = 0;
            chRest = 0;
        }
    }
    if (nRestCount > 0) {
        result.push_back(chRest + 0x3c);
    }
    if (!result.empty()) {
        result.insert(0, sizeof(char), 0x23);
        result.push_back(0x21);
    }
    return result;
}

std::string String::WemadeDecode(const std::string &in) {
    std::string strSrc = in;
    std::string result;
    if (strSrc.empty()) {
        return result;
    }
    auto endPos = strSrc.rfind(0x21);
    if (endPos == std::string::npos) {
        return result;
    } else {
        strSrc.erase(endPos, strSrc.size() - endPos);
        strSrc.push_back(0x21);
    }
    if (0x23 != strSrc.front() || 0x21 != strSrc.back()) {
        return result;
    }
    strSrc.erase(strSrc.begin());
    strSrc.pop_back();
    const unsigned char Decode6BitMask[5] = {0xfc, 0xf8, 0xf0, 0xe0, 0xc0};
    int nDestPos = 0, nBitPos = 2;
    int nMadeBit = 0;
    unsigned char ch, chCode, tmp;
    for (auto it = strSrc.begin(); it != strSrc.end(); ++it) {
        if ((*it - 0x3c) >= 0) {
            ch = *it - 0x3c;
        } else {
            nDestPos = 0;
            break;
        }
        if ((nMadeBit + 6) >= 8) {
            chCode = (tmp | ((ch & 0x3f) >> (6 - nBitPos)));
            result.push_back(chCode);
            nMadeBit = 0;
            if (nBitPos < 6) {
                nBitPos += 2;
            } else {
                nBitPos = 2;
                continue;
            }
        }
        tmp = ((ch << nBitPos) & Decode6BitMask[nBitPos - 2]);
        nMadeBit += (8 - nBitPos);
    }
    return result;
}
