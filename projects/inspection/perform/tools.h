#ifndef __PROJECTS_INSPECTION_PERFORM_TOOLS_H_
#define __PROJECTS_INSPECTION_PERFORM_TOOLS_H_

struct WindowInfo {
  HWND hwnd;
  std::string title;
  DWORD pid;
  std::string imagePathname;
};

class Tools {
public:
  Tools();
  void Release() const;

private:
  ~Tools();
  void Init();
  void UnInit();

public:
  const std::string &CurrentProcessPath() const;
  void GetTaskbarProcesses() const;

private:
  std::string current_process_path_;
};

#endif ///__PROJECTS_INSPECTION_PERFORM_TOOLS_H_