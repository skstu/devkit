#include "stdafx.h"

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  std::vector<WindowInfo> *windows =
      reinterpret_cast<std::vector<WindowInfo> *>(lParam);

  if (IsWindowVisible(hwnd)) {
    int length = GetWindowTextLengthA(hwnd);
    if (length > 0) {
      std::string title(length, '\0');
      GetWindowTextA(hwnd, &title[0], length + 1);

      DWORD pid;
      GetWindowThreadProcessId(hwnd, &pid);

      HANDLE hProcess = nullptr;
      std::string imagePathname;
      do {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess)
          break;
        char szImage[MAX_PATH] = {0};
        DWORD dwImageLen =
            GetModuleFileNameExA(hProcess, NULL, szImage, sizeof(szImage));
        if (dwImageLen <= 0)
          break;
        imagePathname.append(szImage, dwImageLen);
        imagePathname = stl::Path::NormalA(imagePathname);
        for (auto it = imagePathname.begin(); it != imagePathname.end(); ++it) {
          if (*it == '\\')
            *it = '/';
        }
      } while (0);
      windows->emplace_back(WindowInfo{hwnd, title, pid, imagePathname});
    }
  }
  return TRUE;
}

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
}
void Tools::UnInit() {
}
const std::string &Tools::CurrentProcessPath() const {
  return current_process_path_;
}
void Tools::GetTaskbarProcesses() const {
  std::vector<WindowInfo> windows;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));

  rapidjson::Document doc(rapidjson::Type::kArrayType);
  for (const auto &window : windows) {
    rapidjson::Value vObj(rapidjson::Type::kObjectType);
    /*std::wcout << L"HWND: " << window.hwnd << L", Title: " << window.title
               << L", PID: " << window.pid << std::endl;*/
    vObj.AddMember(rapidjson::Value().SetString("pid", doc.GetAllocator()),
                   rapidjson::Value().SetUint(window.pid), doc.GetAllocator());

    vObj.AddMember(
        rapidjson::Value().SetString("title", doc.GetAllocator()).Move(),
        rapidjson::Value()
            .SetString(window.title.c_str(), doc.GetAllocator())
            .Move(),
        doc.GetAllocator());

    vObj.AddMember(
        rapidjson::Value().SetString("handle", doc.GetAllocator()).Move(),
        rapidjson::Value().SetUint64(reinterpret_cast<uint64_t>(window.hwnd)),
        doc.GetAllocator());

    vObj.AddMember(
        rapidjson::Value().SetString("path", doc.GetAllocator()).Move(),
        rapidjson::Value()
            .SetString(window.imagePathname.c_str(), doc.GetAllocator())
            .Move(),
        doc.GetAllocator());

    doc.PushBack(vObj, doc.GetAllocator());
    /*LOG_OUTPUT("HWND({}),Title({}),PID({})",
               reinterpret_cast<uint64_t>(window.hwnd), window.title,
               window.pid);*/
  }

  std::string final_pathname =
      current_process_path_ + "\\shared\\processes.json";

  std::string ansi = Json::toString(doc);

  char *to_name = nullptr;
  size_t to_nameLen = 0;
  sk_icu_match_type_to_name(sk_icu_conv_type::ICU_CONV_TYPE_UTF8, &to_name,
                            &to_nameLen);
  char *res = nullptr;
  size_t resLen = 0;
  sk_icu_convert(ansi.data(), ansi.size(), to_name, &res, &resLen);
  std::string final_json(res, resLen);
  stl::File::Create(final_pathname);
  stl::File::WriteFile(final_pathname, final_json);
  // sk_mem_free((void **)&to_name);
  // sk_mem_free((void **)&res);
}