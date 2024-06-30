#include "stdafx.h"

namespace shared {

 bool Win::Encryption::MD5(const std::string& input, std::string& output) {
  bool result = false;
  output.clear();
  HCRYPTPROV hProv = NULL;
  HCRYPTPROV hHash = NULL;
  do {
   if (::CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) == FALSE)
    break;
   if (::CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash) == FALSE)
    break;
   if (::CryptHashData(hHash, (BYTE*)input.data(), static_cast<DWORD>(input.size()), NULL) == FALSE)
    break;
   DWORD dwHashLen = sizeof(DWORD);
   if (::CryptGetHashParam(hHash, HP_HASHVAL, NULL, &dwHashLen, 0) == FALSE)
    break;
   output.resize(dwHashLen, 0x00);
   if (::CryptGetHashParam(hHash, HP_HASHVAL, (BYTE*)&output[0], &dwHashLen, 0) == FALSE)
    break;
   result = true;
  } while (0);
  if (hHash) {
   if (TRUE == ::CryptDestroyHash(hHash))
    hHash = NULL;
  }
  if (hProv) {
   if (TRUE == ::CryptReleaseContext(hProv, 0))
    hProv = NULL;
  }
  return result;
 }
 std::string Win::Encryption::MD5(const std::string& input, const bool& lower /*= true*/) {
  std::string result;
  do {
   if (!MD5(input, result) || result.empty())
    break;
   result = shared::Win::BinToHex(result);
   if (!lower)
    result = IConv::ToUpperA(result);
  } while (0);
  return result;
 }

 std::string Win::Encryption::WemadeEncode(const std::string& input) {
  std::string result;
  size_t nSrcLen = input.length();
  std::uint8_t* pszSrc = (std::uint8_t*)input.c_str();
  int nDestPos = 0;
  int nRestCount = 0;
  std::uint8_t	chMade = 0;
  std::uint8_t chRest = 0;
  for (size_t i = 0; i < nSrcLen; i++) {
   chMade = ((chRest | (pszSrc[i] >> (2 + nRestCount))) & 0x3f);
   chRest = (((pszSrc[i] << (8 - (2 + nRestCount))) >> 2) & 0x3f);
   nRestCount += 2;
   if (nRestCount < 6) {
    result.push_back(chMade + 0x3c);
   }
   else {
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

 std::string Win::Encryption::WemadeDecode(const std::string& input) {
  std::string result;
  std::string strSrc = input;
  if (strSrc.empty()) {
   return result;
  }
  size_t endPos = strSrc.rfind(0x21);
  if (endPos == std::string::npos) {
   return result;
  }
  else {
   strSrc.erase(endPos, strSrc.size() - endPos);
   strSrc.push_back(0x21);
  }
  if (0x23 != strSrc.front() || 0x21 != strSrc.back()) {
   return result;
  }
  strSrc.erase(strSrc.begin());
  strSrc.pop_back();
  const std::uint8_t Decode6BitMask[5] = { 0xfc, 0xf8, 0xf0, 0xe0, 0xc0 };
  int nDestPos = 0;
  int nBitPos = 2;
  int nMadeBit = 0;
  std::uint8_t	 ch = 0;
  std::uint8_t chCode = 0;
  std::uint8_t tmp = 0;
  for (auto it = strSrc.begin(); it != strSrc.end(); ++it) {
   if ((*it - 0x3c) >= 0) {
    ch = *it - 0x3c;
   }
   else {
    nDestPos = 0;
    break;
   }
   if ((nMadeBit + 6) >= 8) {
    chCode = (tmp | ((ch & 0x3f) >> (6 - nBitPos)));
    result.push_back(chCode);
    nMadeBit = 0;
    if (nBitPos < 6)
     nBitPos += 2;
    else {
     nBitPos = 2;
     continue;
    }
   }
   tmp = ((ch << nBitPos) & Decode6BitMask[nBitPos - 2]);
   nMadeBit += (8 - nBitPos);
  }
  return result;
 }



 namespace base64 {
  /*
   base64.cpp and base64.h

   base64 encoding and decoding with C++.
   More information at
     https://renenyffenegger.ch/notes/development/Base64/Encoding-and-decoding-base-64-with-cpp

   Version: 2.rc.09 (release candidate)

   Copyright (C) 2004-2017, 2020-2022 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

//
// Depending on the url parameter in base64_chars, one of
// two sets of base64 characters needs to be chosen.
// They differ in their last two characters.
//
  static const char* base64_chars[2] = {
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789"
               "+/",

               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789"
               "-_" };

  static unsigned int pos_of_char(const unsigned char chr) {
   //
   // Return the position of chr within base64_encode()
   //

   if (chr >= 'A' && chr <= 'Z') return chr - 'A';
   else if (chr >= 'a' && chr <= 'z') return chr - 'a' + ('Z' - 'A') + 1;
   else if (chr >= '0' && chr <= '9') return chr - '0' + ('Z' - 'A') + ('z' - 'a') + 2;
   else if (chr == '+' || chr == '-') return 62; // Be liberal with input and accept both url ('-') and non-url ('+') base 64 characters (
   else if (chr == '/' || chr == '_') return 63; // Ditto for '/' and '_'
   else
    //
    // 2020-10-23: Throw std::exception rather than const char*
    //(Pablo Martin-Gomez, https://github.com/Bouska)
    //
    throw std::runtime_error("Input is not valid base64-encoded data.");
  }

  static std::string insert_linebreaks(std::string str, size_t distance) {
   //
   // Provided by https://github.com/JomaCorpFX, adapted by me.
   //
   if (!str.length()) {
    return "";
   }

   size_t pos = distance;

   while (pos < str.size()) {
    str.insert(pos, "\n");
    pos += distance + 1;
   }

   return str;
  }

  template <typename String, unsigned int line_length>
  static std::string encode_with_line_breaks(String s) {
   return insert_linebreaks(Win::Encryption::base64_encode(s, false), line_length);
  }

  template <typename String>
  static std::string encode_pem(String s) {
   return encode_with_line_breaks<String, 64>(s);
  }

  template <typename String>
  static std::string encode_mime(String s) {
   return encode_with_line_breaks<String, 76>(s);
  }

  template <typename String>
  static std::string encode(String s, bool url) {
   return Win::Encryption::base64_encode(reinterpret_cast<const unsigned char*>(s.data()), s.length(), url);
  }

  template <typename String>
  static std::string decode(String const& encoded_string, bool remove_linebreaks) {
   //
   // decode(…) is templated so that it can be used with String = const std::string&
   // or std::string_view (requires at least C++17)
   //

   if (encoded_string.empty()) return std::string();

   if (remove_linebreaks) {

    std::string copy(encoded_string);

    copy.erase(std::remove(copy.begin(), copy.end(), '\n'), copy.end());

    return Win::Encryption::base64_decode(copy, false);
   }

   size_t length_of_string = encoded_string.length();
   size_t pos = 0;

   //
   // The approximate length (bytes) of the decoded string might be one or
   // two bytes smaller, depending on the amount of trailing equal signs
   // in the encoded string. This approximation is needed to reserve
   // enough space in the string to be returned.
   //
   size_t approx_length_of_decoded_string = length_of_string / 4 * 3;
   std::string ret;
   ret.reserve(approx_length_of_decoded_string);

   while (pos < length_of_string) {
    //
    // Iterate over encoded input string in chunks. The size of all
    // chunks except the last one is 4 bytes.
    //
    // The last chunk might be padded with equal signs or dots
    // in order to make it 4 bytes in size as well, but this
    // is not required as per RFC 2045.
    //
    // All chunks except the last one produce three output bytes.
    //
    // The last chunk produces at least one and up to three bytes.
    //

    size_t pos_of_char_1 = pos_of_char(encoded_string.at(pos + 1));

    //
    // Emit the first output byte that is produced in each chunk:
    //
    ret.push_back(static_cast<std::string::value_type>(((pos_of_char(encoded_string.at(pos + 0))) << 2) + ((pos_of_char_1 & 0x30) >> 4)));

    if ((pos + 2 < length_of_string) &&  // Check for data that is not padded with equal signs (which is allowed by RFC 2045)
     encoded_string.at(pos + 2) != '=' &&
     encoded_string.at(pos + 2) != '.'         // accept URL-safe base 64 strings, too, so check for '.' also.
     )
    {
     //
     // Emit a chunk's second byte (which might not be produced in the last chunk).
     //
     unsigned int pos_of_char_2 = pos_of_char(encoded_string.at(pos + 2));
     ret.push_back(static_cast<std::string::value_type>(((pos_of_char_1 & 0x0f) << 4) + ((pos_of_char_2 & 0x3c) >> 2)));

     if ((pos + 3 < length_of_string) &&
      encoded_string.at(pos + 3) != '=' &&
      encoded_string.at(pos + 3) != '.'
      )
     {
      //
      // Emit a chunk's third byte (which might not be produced in the last chunk).
      //
      ret.push_back(static_cast<std::string::value_type>(((pos_of_char_2 & 0x03) << 6) + pos_of_char(encoded_string.at(pos + 3))));
     }
    }

    pos += 4;
   }

   return ret;
  }

 }///namespace base64

 std::string Win::Encryption::base64_encode(unsigned char const* bytes_to_encode, size_t in_len, bool url) {

  size_t len_encoded = (in_len + 2) / 3 * 4;

  unsigned char trailing_char = url ? '.' : '=';

  //
  // Choose set of base64 characters. They differ
  // for the last two positions, depending on the url
  // parameter.
  // A bool (as is the parameter url) is guaranteed
  // to evaluate to either 0 or 1 in C++ therefore,
  // the correct character set is chosen by subscripting
  // base64_chars with url.
  //
  const char* base64_chars_ = base64::base64_chars[url];

  std::string ret;
  ret.reserve(len_encoded);

  unsigned int pos = 0;

  while (pos < in_len) {
   ret.push_back(base64_chars_[(bytes_to_encode[pos + 0] & 0xfc) >> 2]);

   if (pos + 1 < in_len) {
    ret.push_back(base64_chars_[((bytes_to_encode[pos + 0] & 0x03) << 4) + ((bytes_to_encode[pos + 1] & 0xf0) >> 4)]);

    if (pos + 2 < in_len) {
     ret.push_back(base64_chars_[((bytes_to_encode[pos + 1] & 0x0f) << 2) + ((bytes_to_encode[pos + 2] & 0xc0) >> 6)]);
     ret.push_back(base64_chars_[bytes_to_encode[pos + 2] & 0x3f]);
    }
    else {
     ret.push_back(base64_chars_[(bytes_to_encode[pos + 1] & 0x0f) << 2]);
     ret.push_back(trailing_char);
    }
   }
   else {

    ret.push_back(base64_chars_[(bytes_to_encode[pos + 0] & 0x03) << 4]);
    ret.push_back(trailing_char);
    ret.push_back(trailing_char);
   }

   pos += 3;
  }


  return ret;
 }

 std::string Win::Encryption::base64_decode(std::string const& s, bool remove_linebreaks) {
  return base64::decode(s, remove_linebreaks);
 }

 std::string Win::Encryption::base64_encode(std::string const& s, bool url) {
  return base64::encode(s, url);
 }

 std::string Win::Encryption::base64_encode_pem(std::string const& s) {
  return base64::encode_pem(s);
 }

 std::string Win::Encryption::base64_encode_mime(std::string const& s) {
  return base64::encode_mime(s);
 }

#if __cplusplus >= 201703L
 //
 // Interface with std::string_view rather than const std::string&
 // Requires C++17
 // Provided by Yannic Bonenberger (https://github.com/Yannic)
 //

 std::string Win::Encryption::base64_encode(std::string_view s, bool url) {
  return base64::encode(s, url);
 }

 std::string Win::Encryption::base64_encode_pem(std::string_view s) {
  return base64::encode_pem(s);
 }

 std::string Win::Encryption::base64_encode_mime(std::string_view s) {
  return base64::encode_mime(s);
 }

 std::string Win::Encryption::base64_decode(std::string_view s, bool remove_linebreaks) {
  return base64::decode(s, remove_linebreaks);
 }

#endif  // __cplusplus >= 201703L

}///namespace 