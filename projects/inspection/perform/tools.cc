#include "stdafx.h"

Tools::Tools() {
  Init();
}

Tools::~Tools() {
  UnInit();
}

void Tools::Release() const {
  delete this;
}

void Tools::Init() {
  current_process_path_ = sk::GetCurrentPath();
  data_dir_ = current_process_path_ + R"(\data\)";
  shared_file_capture_ = current_process_path_ + R"(\shared\capture.json)";
  shared_file_processes_ = current_process_path_ + R"(\shared\processes.json)";

#ifdef _DEBUG
  gpWin = win::IWin::Create(
      R"(C:\Users\k34ub\source\skstu\devkit\bin\projects\x64\Release\win.dll)");
#else
  gpWin = win::IWin::Create("win.dll");
#endif
  gpOverlay = inspect::IOverlay::Create("overlay.dll");
  gpUiohook = uiohook::IUiohook::Create("uiohook.dll");
  gpAutomation = inspect::IAutomation::Create("automation.dll");
}
void Tools::UnInit() {
  uiohook::IUiohook::Destroy(&gpUiohook);
  inspect::IOverlay::Destroy(&gpOverlay);
  inspect::IAutomation::Destroy(&gpAutomation);
  win::IWin::Destroy(&gpWin);
}
const std::string &Tools::CurrentProcessPath() const {
  return current_process_path_;
}
void Tools::OnCaptureFinished(const inspect::IElement *pElement,
                              const std::string &imgBuffer) const {
  do {
    if (!pElement || imgBuffer.empty())
      break;

    char *base64Img = nullptr;
    size_t base64ImgLen = 0;
    gpWin->Base64Encode(imgBuffer.data(), imgBuffer.size(), &base64Img,
                        &base64ImgLen);
    std::string strBase64Img(base64Img, base64ImgLen);
    strBase64Img.insert(0, R"(data:image/png;base64,)");
    gpWin->FreePtr((void **)&base64Img);
    rapidjson::Document doc(rapidjson::Type::kObjectType);

    doc.AddMember(rapidjson::Value().SetString("code", doc.GetAllocator()),
                  rapidjson::Value().SetInt(0), doc.GetAllocator());
    doc.AddMember(rapidjson::Value().SetString("msg", doc.GetAllocator()),
                  rapidjson::Value().SetString("ok", doc.GetAllocator()),
                  doc.GetAllocator());

    rapidjson::Value dataObj(rapidjson::Type::kObjectType);

    dataObj.AddMember(
        rapidjson::Value().SetString("BmpData", doc.GetAllocator()),
        rapidjson::Value().SetString(strBase64Img.c_str(), doc.GetAllocator()),
        doc.GetAllocator());

    char *u8name = nullptr;
    size_t u8nameLen = 0;
    gpWin->WStringToUTF8(pElement->Name(), wcslen(pElement->Name()), &u8name,
                         &u8nameLen);
    dataObj.AddMember(
        rapidjson::Value().SetString("WebText", doc.GetAllocator()),
        rapidjson::Value().SetString(
            u8name ? std::string(u8name, u8nameLen).c_str() : "",
            doc.GetAllocator()),
        doc.GetAllocator());
    gpWin->FreePtr((void **)&u8name);

    dataObj.AddMember(
        rapidjson::Value().SetString("key", doc.GetAllocator()),
        rapidjson::Value().SetString(pElement->Identify(), doc.GetAllocator()),
        doc.GetAllocator());
    dataObj.AddMember(
        rapidjson::Value().SetString("sha1", doc.GetAllocator()),
        rapidjson::Value().SetString(pElement->Sha1(), doc.GetAllocator()),
        doc.GetAllocator());
    dataObj.AddMember(
        rapidjson::Value().SetString("sha256", doc.GetAllocator()),
        rapidjson::Value().SetString(pElement->Sha256(), doc.GetAllocator()),
        doc.GetAllocator());

    doc.AddMember(rapidjson::Value().SetString("data", doc.GetAllocator()),
                  dataObj, doc.GetAllocator());

    std::string final_json = Json::toString(doc);
    stl::File::Create(shared_file_capture_);
    stl::File::WriteFile(shared_file_capture_, final_json);
    // std::vector<const inspect::IElement *> eles;
    // eles.emplace_back(pElement);
    //  GenTaskbarProcessesCapture(eles, data);
  } while (0);
}
void Tools::OnCaptureFinished(const inspect::IElement *pElement,
                              const inspect::IOverlay::IStream *data) const {
  do {
    if (!pElement || !data)
      break;
    if (data->Empty())
      break;

    char *base64Img = nullptr;
    size_t base64ImgLen = 0;
    gpWin->Base64Encode((char *)data->GetBuffer(), data->GetSize(), &base64Img,
                        &base64ImgLen);
    std::string strBase64Img(base64Img, base64ImgLen);
    strBase64Img.insert(0, R"(data:image/png;base64,)");
    gpWin->FreePtr((void **)&base64Img);
    rapidjson::Document doc(rapidjson::Type::kObjectType);

    doc.AddMember(rapidjson::Value().SetString("code", doc.GetAllocator()),
                  rapidjson::Value().SetInt(0), doc.GetAllocator());
    doc.AddMember(rapidjson::Value().SetString("msg", doc.GetAllocator()),
                  rapidjson::Value().SetString("ok", doc.GetAllocator()),
                  doc.GetAllocator());

    rapidjson::Value dataObj(rapidjson::Type::kObjectType);

    dataObj.AddMember(
        rapidjson::Value().SetString("BmpData", doc.GetAllocator()),
        rapidjson::Value().SetString(strBase64Img.c_str(), doc.GetAllocator()),
        doc.GetAllocator());

    char *u8name = nullptr;
    size_t u8nameLen = 0;
    gpWin->WStringToUTF8(pElement->Name(), wcslen(pElement->Name()), &u8name,
                         &u8nameLen);
    dataObj.AddMember(
        rapidjson::Value().SetString("WebText", doc.GetAllocator()),
        rapidjson::Value().SetString(
            u8name ? std::string(u8name, u8nameLen).c_str() : "",
            doc.GetAllocator()),
        doc.GetAllocator());
    gpWin->FreePtr((void **)&u8name);

    dataObj.AddMember(
        rapidjson::Value().SetString("key", doc.GetAllocator()),
        rapidjson::Value().SetString(pElement->Identify(), doc.GetAllocator()),
        doc.GetAllocator());

    doc.AddMember(rapidjson::Value().SetString("data", doc.GetAllocator()),
                  dataObj, doc.GetAllocator());

    std::string final_json = Json::toString(doc);
    stl::File::Create(shared_file_capture_);
    stl::File::WriteFile(shared_file_capture_, final_json);
    std::vector<const inspect::IElement *> eles;
    eles.emplace_back(pElement);
    GenTaskbarProcessesCapture(eles, data);
  } while (0);
}
void Tools::OnCaptureCanceled() const {
  stl::File::Create(shared_file_capture_);
  stl::File::WriteFile(shared_file_capture_,
                       R"({"code":3,"msg":"user canceled"})");
}
void Tools::GenTaskbarProgramData(const std::string &elekey,
                                  const std::string &data) const {
  do {
    if (elekey.empty() || data.empty())
      break;
    std::string pro_wnd = data_dir_ + "\\" + elekey + "\\" + file_pro_wnd_;
    stl::File::Create(pro_wnd);
    stl::File::WriteFile(pro_wnd, data);
  } while (0);
}
void Tools::GenTaskbarProcessesCapture(
    const std::vector<const inspect::IElement *> &eles,
    const inspect::IOverlay::IStream *data) const {

  for (const auto &ele : eles) {
    std::string ele_bin_path =
        data_dir_ + "\\" + ele->Identify() + "\\" + file_ele_bin_;
    std::string ele_png_path =
        data_dir_ + "\\" + ele->Identify() + "\\" + file_ele_png_;

    char *ele_data = nullptr;
    size_t ele_data_len = 0;
    ele->Serialization(&ele_data, &ele_data_len);
    std::string final_data(ele_data, ele_data_len);
    gpAutomation->FreePtr((void **)&ele_data);
    stl::File::Create(ele_bin_path);
    stl::File::WriteFile(ele_bin_path, final_data);
    if (data) {
      stl::File::WriteFile(ele_png_path, std::string((char *)data->GetBuffer(),
                                                     data->GetSize()));
    }
  }
}
void Tools::GetTaskbarProcesses() const {
  do {
    if (!gpWin || !gpAutomation)
      break;
    if (!gpAutomation->Start())
      break;
    win::IWindowInfoArray *windowArray = nullptr;
    gpWin->EnumWindows(&windowArray);
    if (!windowArray)
      return;
    std::vector<const inspect::IElement *> eles;
    std::string final_json;
    size_t total = windowArray->Total();
    for (size_t i = 0; i < total; ++i) {
      win::IWindowInfo *info = windowArray->Next(i);
      if (!info)
        continue;
      HWND hwnd = (HWND)info->Hwnd();
      if (!IsWindowVisible(hwnd))
        continue;
      if (GetWindowTextLengthW(hwnd) <= 0)
        continue;
      std::wstring wstrPath(info->ImagePathname());
      std::wstring wstrClassName(info->WindowClass());
      if (wstrPath.find(L".exe") == std::wstring::npos)
        continue;
      if (wstrPath.find(L"explorer.exe") != std::wstring::npos)
        continue;
      if (wstrClassName.find(L"Windows.UI.Core.CoreWindow") !=
          std::wstring::npos)
        continue;
      if (wstrClassName.find(L"ApplicationFrameWindow") != std::wstring::npos)
        continue;
      // StrRStrIW(()
      /*if (wcscmp(L"安装", info->WindowText()) == 0) {
        MessageBoxW(NULL, info->ImagePathname(), NULL, MB_TOPMOST);
      }*/

      inspect::IElement *pElement =
          gpAutomation->GetElementFromHwnd(info->Hwnd());
      if (!pElement)
        continue;
      eles.emplace_back(pElement);
      info->SetKey(pElement->Identify());
      std::string strJson(info->toJson());
      GenTaskbarProgramData(pElement->Identify(), strJson);
      final_json.append(strJson).append(",");
    }
    if (!final_json.empty())
      final_json.pop_back();
    final_json.insert(0, "[");
    final_json.push_back(']');
    windowArray->Release();
    stl::File::Create(shared_file_processes_);
    stl::File::WriteFile(shared_file_processes_, final_json);
    GenTaskbarProcessesCapture(eles);
  } while (0);
}

void Tools::OnCaptureFinished(const inspect::IElementArray *pElements) const {
  do {
    if (!pElements)
      break;

    std::cout << fmt::format("Capture total ({}).", pElements->Total())
              << std::endl;
    /*MessageBoxA(NULL,
                fmt::format("Capture total ({}).", pElements->Total()).c_str(),
                NULL, MB_TOPMOST);*/
    for (size_t i = 0; i < pElements->Total(); ++i) {
      auto pElement = pElements->Next(i);

      std::cout << pElement->Identify() << std::endl;
      std::cout << pElement->Sha1() << std::endl;
      std::cout << pElement->Sha256() << std::endl;
      std::string current_path =
          stl::Path::FixA(data_dir_ + "\\" + pElement->Identify(), "\\");
      gpWin->CreateFolder(current_path.c_str());
      std::string final_file_bin = current_path + "\\" + file_ele_bin_;
      std::string final_file_png = current_path + "\\" + file_ele_png_;
      char *imgBuffer = nullptr;
      size_t imgBufferSize = 0;
      char *eleBin = nullptr;
      size_t eleBinSize = 0;
      pElement->Screenshot();
      pElement->ImageStream(&imgBuffer, &imgBufferSize);
      pElement->Serialization(&eleBin, &eleBinSize);
      std::string strImgBuffer(imgBuffer, imgBufferSize);
      std::string strEleBin(eleBin, eleBinSize);
      gpAutomation->FreePtr((void **)&eleBin);
      gpAutomation->FreePtr((void **)&imgBuffer);

      stl::File::WriteFile(final_file_png, strImgBuffer);
      stl::File::WriteFile(final_file_bin, strEleBin);
      if (i == pElements->Total() - 1) {
        OnCaptureFinished(pElement, strImgBuffer);
      }
    }
  } while (0);
}
void Tools::TestFindElement() const {
  const std::string ele_desktop_file =
      R"(C:\Users\k34ub\Desktop\RPA\TestData\DeskFile\bd81b323a06c1710c12e606899aeeee9\ele.bin)";
  const std::string ele_dd_send_message_dit =
      R"(C:\Users\k34ub\Desktop\RPA\TestData\DDInput\468c6e0945ede688a14463d403def7f5\ele.bin)";
#if 1
  do {
    if (!gpWin || !gpAutomation)
      break;
    if (!gpAutomation->Start())
      break;
#if 1 // DD
    gpAutomation->Setup(R"(C:\Users\k34ub\Desktop\RPA\TestData\DDInput\)");
    std::string bin_data = stl::File::ReadFile(ele_dd_send_message_dit);
    auto pElement =
        gpAutomation->CreateElementFromBin(bin_data.data(), bin_data.size());
#else // FILE
    gpAutomation->Setup(R"(C:\Users\k34ub\Desktop\RPA\TestData\DeskFile\)");
    std::string bin_data = stl::File::ReadFile(ele_desktop_file);
    auto pElement =
        gpAutomation->CreateElementFromBin(bin_data.data(), bin_data.size());
#endif
    auto pFoundElement = gpAutomation->FindElementFormRoot(pElement, true);
    gpAutomation->Stop();
  } while (0);
#endif
}
///////////////////////////////////////////////////////////////////////////////
inspect::IOverlay *gpOverlay = nullptr;
uiohook::IUiohook *gpUiohook = nullptr;
inspect::IAutomation *gpAutomation = nullptr;
Tools *gpTools = nullptr;
win::IWin *gpWin = nullptr;