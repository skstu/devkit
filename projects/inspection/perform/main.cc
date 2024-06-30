#include "stdafx.h"
static std::atomic_bool gs_system_exit = false;
static std::atomic_bool gs_screenshot_working = false;

static void Screenshot() {
  gs_screenshot_working.store(true);
  gpOverlay->ConfigGet()->EnableScreenshot(true);
  gs_system_exit.store(true);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd) {

  gpTools = new Tools();

  LOG_INIT(gpTools->CurrentProcessPath() + "\\perform.log");
#if 0
  gpTools->TestFindElement();
  return 0;
#endif

  do {
    if (lpCmdLine == nullptr) {
      break;
    }
    std::string strCmdLine(lpCmdLine);
    std::vector<std::string> args = stl::String::StringSpilt(strCmdLine, " ");
    for (const auto &arg : args) {
      if (arg == "") {
        continue;
      }
      if (arg == "--get-processes") {
        gpTools->GetTaskbarProcesses();
        if (gpAutomation) {
          gpAutomation->Stop();
          inspect::IAutomation::Destroy(&gpAutomation);
        }

        return 0;
      }
    }
    // lpCmdLine
  } while (0);

  do {
    if (!gpUiohook || !gpOverlay || !gpAutomation)
      break;
    gpAutomation->RegisterCaptureFinishedCb(
        reinterpret_cast<unsigned long long>(gpOverlay),
        [](const unsigned long long &ptr, const inspect::IElement *pElement) {
          inspect::IOverlay *overlay =
              reinterpret_cast<inspect::IOverlay *>(ptr);
          if (overlay) {
            overlay->SetPosition(pElement->Left(), pElement->Top(),
                                 pElement->Width(), pElement->Height());
          }
        });
    gpUiohook->RegisterEventCb(100ull, [](const uiohook::IUioEvent *pEvent) {
      switch (pEvent->Type()) {
      case uiohook::UioEventType::UIO_EVENT_KEY_RELEASED:
        if (pEvent->DataKeyboard()->Keycode() ==
            static_cast<uint16_t>(uiohook::UioVirtualKeyCodes::UIO_VC_ESCAPE)) {
          gpTools->OnCaptureCanceled();
          gs_system_exit.store(true);
          break;
        }
        break;
      case uiohook::UioEventType::UIO_EVENT_KEY_PRESSED:
        if (pEvent->IsKeyPressed(
                uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_L) ||
            pEvent->IsKeyPressed(
                uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_R)) {

          if (pEvent->DataKeyboard()->Keycode() ==
              static_cast<uint16_t>(uiohook::UioVirtualKeyCodes::UIO_VC_F1)) {
            if (gpAutomation->ConfigGet()->EnableMouseMoveEvent()) {
              gpAutomation->ConfigGet()->EnableMouseMoveEvent(false);
              MessageBoxA(NULL, "CTRL+F1 Mouse move event close.", NULL,
                          MB_TOPMOST);
            } else {
              gpAutomation->ConfigGet()->EnableMouseMoveEvent(true);
              MessageBoxA(NULL, "CTRL+F1 Mouse move event open.", NULL,
                          MB_TOPMOST);
            }

          } else if (pEvent->DataKeyboard()->Keycode() ==
                     static_cast<uint16_t>(
                         uiohook::UioVirtualKeyCodes::UIO_VC_F2)) {
            if (gpAutomation->ConfigGet()->EnableMouseClickedEvent()) {
              MessageBoxA(NULL, "CTRL+F2 Mouse clicked event close.", NULL,
                          MB_TOPMOST);
              gpAutomation->ConfigGet()->EnableMouseClickedEvent(false);
            } else {
              MessageBoxA(NULL, "CTRL+F2 Mouse clicked event open.", NULL,
                          MB_TOPMOST);
              gpAutomation->ConfigGet()->EnableMouseClickedEvent(true);
            }
          } else if (pEvent->DataKeyboard()->Keycode() ==
                     static_cast<uint16_t>(
                         uiohook::UioVirtualKeyCodes::UIO_VC_F3)) {
            if (gpAutomation->ConfigGet()->EnableFocusChangedEvent()) {
              MessageBoxA(NULL, "CTRL+F3 Focus changed event close.", NULL,
                          MB_TOPMOST);
              gpAutomation->ConfigGet()->EnableFocusChangedEvent(false);
            } else {
              MessageBoxA(NULL, "CTRL+F3 Focus changed event open.", NULL,
                          MB_TOPMOST);
              gpAutomation->ConfigGet()->EnableFocusChangedEvent(true);
            }
          } else if (pEvent->DataKeyboard()->Keycode() ==
                     static_cast<uint16_t>(
                         uiohook::UioVirtualKeyCodes::UIO_VC_F12)) {
            //!@//!Capture
            Screenshot();
          }
        }

        break;
      case uiohook::UioEventType::UIO_EVENT_MOUSE_MOVED:
        /*if (gpAutomation)
          gpAutomation->OnMouseMove();*/
        break;
      case uiohook::UioEventType::UIO_EVENT_MOUSE_PRESSED:
        if (gpAutomation)
          gpAutomation->OnMouseClicked();

        break;
      case uiohook::UioEventType::UIO_EVENT_MOUSE_RELEASED:
        if (!pEvent->IsKeyPressed(
                uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_L) &&
            !pEvent->IsKeyPressed(
                uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_R))
          break;

        if (uiohook::UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT !=
            uiohook::UioVirtualMouseButtons(pEvent->DataMouse()->Button()))
          break;
        //!@ Capture
        Screenshot();
        break;
      default:
        break;
      }
    });

    // gpUiohook->Start();
    gpOverlay->Start(inspect::OverlayWindowType::OVERLAY_WINDOW_ELE);
    gpAutomation->ConfigGet()->EnableCapture(true);
    gpAutomation->Start();
    do {
      if (gs_system_exit.load() && !gs_screenshot_working.load()) {
        // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        break;
      } else if (gs_screenshot_working.load()) {
        inspect::IElementArray *pElements = gpAutomation->GetElementCapture();
        if (pElements)
          gpTools->OnCaptureFinished(pElements);
        pElements->Release();
        gs_screenshot_working.store(false);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } while (1);
  } while (0);

  if (gpUiohook) {
    gpOverlay->Stop();
    gpAutomation->Stop();
    gpUiohook->UnregisterEvent(100ull);
    gpUiohook->Stop();
  }

  gpTools->Release();
  LOG_UNINIT;
  return 0;
}
