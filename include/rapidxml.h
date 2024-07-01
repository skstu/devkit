#ifndef __RAPID_XML_H_
#define __RAPID_XML_H_

#include "../3rdlocal/rapidxml/rapidxml.hpp"
#include "../3rdlocal/rapidxml/rapidxml_iterators.hpp"
#include "../3rdlocal/rapidxml/rapidxml_print.hpp"
#include "../3rdlocal/rapidxml/rapidxml_utils.hpp"

using tf_apidxml_char_doc = rapidxml::xml_document<char>;
using tf_rapidxml_wchar_doc = rapidxml::xml_document<wchar_t>;

static tf_rapidxml_wchar_doc *rapidxml_parse(const std::wstring &input_data) {
  auto pDoc = new tf_rapidxml_wchar_doc();
  try {
    pDoc->parse<0>((wchar_t *)input_data.c_str());
  } catch (rapidxml::parse_error &err) {
    auto what = err.what();
    if (pDoc) {
      delete pDoc;
      pDoc = nullptr;
    }
  }
  return pDoc;
}

static tf_apidxml_char_doc *rapidxml_parse(const std::string &input_data) {
  auto pDoc = new tf_apidxml_char_doc();
  try {
    pDoc->parse<0>((char *)input_data.c_str());
  } catch (rapidxml::parse_error &err) {
    auto what = err.what();
    if (pDoc) {
      delete pDoc;
      pDoc = nullptr;
    }
  }
  return pDoc;
}
static std::string rapidxml_output(rapidxml::xml_node<char> *input_xml_node) {
  std::string result;
  do {
    if (!input_xml_node)
      break;
    rapidxml::print(std::back_inserter(result), *input_xml_node, 0);
  } while (0);
  return result;
}

#endif ///__RAPID_XML_H_