#include "stdafx.h"

Inspect::Inspect() {
  Init();
}
Inspect::~Inspect() {
  UnInit();
}
void Inspect::Release() const {
  delete this;
}
bool Inspect::Ready() const {
  return false;
}
Inspection::IConfig *Inspect::ConfigGet() const {
  return dynamic_cast<Inspection::IConfig *>(config_);
}
void Inspect::Init() {
  do {
    if (ready_.load())
      break;
    std::string currentPath = sk::GetCurrentPath();
    std::cout << currentPath << std::endl;
#if _DEBUG
    pCvpp_ = ICvpp::Create(
        R"(C:\Users\k34ub\source\skstu\devkit\bin\win\vs\opencv_win32\x64\Release\cvpp.dll)");
#else
    pCvpp_ = ICvpp::Create("cvpp.dll");
#endif

    pOverlay_ = IOverlay::Create("overlay.dll");
    pUiohook_ = uiohook::IUiohook::Create("uiohook.dll");
    pAutomation_ = IAutomation::Create("automation.dll");
    if (pAutomation_) {
      pAutomation_->RegisterCaptureFinishedCb(
          reinterpret_cast<unsigned long long>(this),
          [](const unsigned long long &ptr, const Element *pElement) {
            auto _this = reinterpret_cast<Inspect *>(ptr);

            /*std::cout << fmt::format("element(l:{},t:{},r:{},b:{})",
                                     pElement->pos.left, pElement->pos.top,
                                     pElement->pos.right, pElement->pos.bottom)
                      << std::endl;*/
            _this->pOverlay_->SetPosition(pElement->X(), pElement->Y(),
                                          pElement->CX(), pElement->CY());
          });
    }

    if (pUiohook_) {
      pUiohook_->RegisterEventCb(
          reinterpret_cast<unsigned long long>(pOverlay_),
          [](const uiohook::IUioEvent *pEvent) {
            auto target = reinterpret_cast<IOverlay *>(pEvent->GetUserData());
            if (target)
              target->OnUioEvent(pEvent);
          });

      pUiohook_->RegisterEventCb(
          reinterpret_cast<unsigned long long>(this),
          [](const uiohook::IUioEvent *pEvent) {
            Inspect *_this = reinterpret_cast<Inspect *>(pEvent->GetUserData());
            switch (pEvent->Type()) {
            case uiohook::UioEventType::UIO_EVENT_MOUSE_MOVED:
              break;
            case uiohook::UioEventType::UIO_EVENT_MOUSE_RELEASED: {
              if (!pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_L) &&
                  !pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_R))
                break;
              _this->pOverlay_->Screenshot(
                  /*OverlayWindowType::OVERLAY_WINDOW_BK |
                  OverlayWindowType::OVERLAY_WINDOW_CV |*/
                  OverlayWindowType::OVERLAY_WINDOW_UI);

            } break;
            case uiohook::UioEventType::UIO_EVENT_MOUSE_WHEEL:
              break;
            case uiohook::UioEventType::UIO_EVENT_KEY_RELEASED: {
              if (!pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_L) &&
                  !pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_R))
                break;
              if (!pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_ALT_L) &&
                  !pEvent->IsKeyPressed(
                      uiohook::UioVirtualKeyCodes::UIO_VC_ALT_R))
                break;

              if (pEvent->DataKeyboard()->Keycode() !=
                      uiohook::UioVirtualKeyCodes::UIO_VC_SHIFT_L &&
                  pEvent->DataKeyboard()->Keycode() !=
                      uiohook::UioVirtualKeyCodes::UIO_VC_SHIFT_R)
                break;

              _this->pOverlay_->Screenshot(
                  OverlayWindowType::OVERLAY_WINDOW_BK);
              std::string srcImgPath;
              std::string dstImgPath;
#if _DEBUG
              srcImgPath = R"(C:\Users\k34ub\Desktop\src.png)";
              dstImgPath = R"(C:\Users\k34ub\Desktop\dst.png)";
#else
              std::string path = sk::GetCurrentPath();
              srcImgPath = fmt::format("{}\\{}.png", path, "src");
              dstImgPath = fmt::format("{}\\{}.png", path, "dst");
#endif
              ICvpp::IStream *srcStream = nullptr;
              ICvpp::IStream *dstStream = nullptr;
              ICvpp::IRange *found = nullptr;
              do {
                std::string srcImg = stl::File::ReadFile(srcImgPath);
                std::string dstImg = stl::File::ReadFile(dstImgPath);
                if (srcImg.empty() || dstImg.empty())
                  break;
                srcStream = _this->pCvpp_->CreateStream();
                dstStream = _this->pCvpp_->CreateStream();
                srcStream->SetData(srcImg.data(), srcImg.size());
                dstStream->SetData(dstImg.data(), dstImg.size());
                found = _this->pCvpp_->Find(srcStream, dstStream, 0.3);
                if (!found)
                  break;
                std::cout << fmt::format("CvFound(x:{},y:{},cx:{},cy:{})",
                                         found->X(), found->Y(), found->CX(),
                                         found->CY())
                          << std::endl;

                _this->pOverlay_->SetPosition(found->X(), found->Y(),
                                              found->CX(), found->CY());
                auto sk = 0;
              } while (0);
              if (!found)
                std::cout << "Not found." << std::endl;
              SK_RELEASE_PTR(found);
              SK_RELEASE_PTR(srcStream);
              SK_RELEASE_PTR(dstStream);
            } break;
            default:
              break;
            }
          });
    }
#if 0
    if (pUiohook_)
      pUiohook_->RegisterMouseMoveCb(
          [](const long &x, const long &y, void *route) {
            // std::string output = fmt::format("pt(x:{},y:{})", x, y);
            //  std::cout << output << std::endl;
          },
          this);
    if (pUiohook_)
      pUiohook_->RegisterCaptureFinishCb(
          [](const long &x, const long &y, bool &exit, void *route) {
      /*std::cout << fmt::format("Capture success on point(x:{},y:{}).", x,
         y)
                << std::endl;*/
      /*exit = false;
      auto pElement = pAutomation_->GetElementOnUnderMouse(x, y);
      if (pElement) {
        std::cout << "Found element." << std::endl;
      } else {
        std::cout << "Not found element." << std::endl;
      }*/
#if 0
        // std::cout << "caprute finished." << std::endl;
        pOverlay_->Screenshot([](const char *buffer, const size_t &buffer_len) {
          // std::cout << buffer_len << std::endl;
          auto pngFilePathname =
              fmt::format(R"(C:\Users\k34ub\Desktop\capute_png_s\{}.png)",
                          stl::Time::TimeStamp());
          if (stl::File::WriteFile(pngFilePathname,
                                   std::string(buffer, buffer_len)))
            std::cout << "Write file success." << std::endl;
          else
            std::cout << "Write file failed." << std::endl;
        });
#endif
          },
          this);
#endif
#if 0
    pAutomation_->RegisterElementCaptureFinishCb([&](const IElement *pElement) {
      if (pElement) {
#if 0
        auto pos = pElement->GetPosition();
        auto pt = pos->GetPoint();
        std::string output = fmt::format("pt(x:{},y:{}) is one element.",
                                         pt->GetX(), pt->GetY());
        // std::cout << output << std::endl;
        auto rect = pos->GetRect();
        pElement->Release();
#endif
        pOverlay_->AppendElement(const_cast<IElement *>(pElement));
      }
    });
#endif
    ready_.store(true);
  } while (0);
}
void Inspect::UnInit() {
  do {
    if (!ready_.load())
      break;

    if (pOverlay_)
      IOverlay::Destroy(&pOverlay_);
    if (pAutomation_)
      IAutomation::Destroy(&pAutomation_);
    if (pUiohook_)
      uiohook::IUiohook::Destroy(&pUiohook_);
    ready_.store(false);
  } while (0);
}
bool Inspect::Start() {
  if (pAutomation_)
    pAutomation_->Start();
  if (pOverlay_)
    pOverlay_->Start(OverlayWindowType::OVERLAY_WINDOW_UI);
  if (pUiohook_)
    pUiohook_->Start();
  return true;
}
void Inspect::Stop() {
  if (pUiohook_)
    pUiohook_->Stop();
  if (pAutomation_)
    pAutomation_->Stop();
  if (pOverlay_)
    pOverlay_->Stop();
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Inspect *__gpInspect = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpInspect) {
      break;
    }
    __gpInspect = new Inspect();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<Inspection *>(__gpInspect));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpInspect) {
      break;
    }
    delete __gpInspect;
    __gpInspect = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif