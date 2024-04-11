#include "skfps.h"
#include "base/base64.h"
#include "base/base_export.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/modp_b64/modp_b64.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"
#include "third_party/rapidjson/include/rapidjson/reader.h"
#include "third_party/rapidjson/include/rapidjson/stream.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/include/rapidjson/writer.h"

#include <fstream>
#include <iostream>
#include <ostream>
#include <string>

namespace base {
namespace sk {
const unsigned long IPC_MSG_HEAD = 0xFFA;
const unsigned long IPC_MSG_TAIL = 0xFFF;
const unsigned long long LOGO_OF_DEVELOPER_BEGIN = 0xFAC9C2D0;
const unsigned long long LOGO_OF_DEVELOPER_END = 0xB4B4AAC1;
void sk_free_s(void **ptr) {
  do {
    if (!(*ptr))
      break;
    free(*ptr);
    *ptr = NULL;
  } while (0);
}
void *sk_malloc(size_t size) {
  void *result = NULL;
  result = malloc(size);
  return result;
}
void *sk_realloc(void *ptr, size_t size) {
  if (size > 0)
    return realloc(ptr, size);
  sk_free_s(&ptr);
  return NULL;
}
int sk_base64encode(const char *src, size_t srcLen, char **dst,
                    size_t *dstLen) {
  int result = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    *dstLen = modp_b64_encode_len(srcLen);
    *dst = (char *)sk_malloc(*dstLen);
    if (!*dst)
      break;
    *dstLen = modp_b64_encode(*dst, src, srcLen);
    if (*dstLen <= 0)
      break;
    result = SK_OK;
  } while (0);
  if (result != SK_OK) {
    *dstLen = 0;
  }
  return result;
}
int sk_base64decode(const char *src, size_t srcLen, char **dst,
                    size_t *dstLen) {
  int result = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    *dst = (char *)sk_malloc(modp_b64_decode_len(srcLen));
    if (!*dst)
      break;
    *dstLen = modp_b64_decode(*dst, src, srcLen);
    if (*dstLen == MODP_B64_ERROR)
      break;
    result = SK_OK;
  } while (0);
  if (result != SK_OK) {
    *dstLen = 0;
  }
  return result;
}

namespace fps {

class Configure {
public:
  Configure(const char *, size_t);
  ~Configure();

public:
  class Account {
  public:
    Account();
    ~Account();

  public:
    unsigned long long identify;
    unsigned int level;
    std::string user;
    std::string pwd;
  };
  class Proxy {
  public:
    Proxy();
    Proxy(const Proxy &);
    ~Proxy();

  public:
    void operator=(const Proxy &);
    bool operator<(const Proxy &);
    bool operator>(const Proxy &);
    bool operator==(const Proxy &);

  public:
    unsigned int type;
    std::string addr;
    unsigned int port;
    std::string name;
    std::string pwd;
  };
  class Fp {
  public:
    Fp();
    ~Fp();

  public:
    class f {
    public:
      f();
      ~f();

    public:
      class vs {
      public:
        vs();
        ~vs();

      public:
        class canvas {
        public:
          canvas();
          ~canvas();

        public:
          const unsigned long long key = 1;
          bool enable = false;
        };
        class webgl {
        public:
          webgl();
          ~webgl();

        public:
          const unsigned long long key = 2;
          bool enable = false;
        };
        class audio {
        public:
          audio();
          ~audio();

        public:
          const unsigned long long key = 4;
          bool enable = false;
        };
        class font {
        public:
          font();
          ~font();

        public:
          const unsigned long long key = 8;
          bool enable = false;
        };
        class clientrects {
        public:
          clientrects();
          ~clientrects();

        public:
          class screen {
          public:
            screen();
            ~screen();

          public:
            unsigned int availLeft = 0;
            unsigned int availTop = 0;
            unsigned int availWidth = 0;
            unsigned int availHeight = 0;
            unsigned int width = 0;
            unsigned int height = 0;
            unsigned int colorDepth = 0;
            unsigned int pixelDepth = 0;
          };

        public:
          const unsigned long long key = 16;
          bool enable = false;
          screen screen_;
        };
        class webrtc {
        public:
          webrtc();
          ~webrtc();

        public:
          const unsigned long long key = 32;
          bool enable = false;
        };
        class navigator {
        public:
          navigator();
          ~navigator();

        public:
          const unsigned long long key = 64;
          bool enable = false;
          std::string appCodeName;
          std::string appName;
          std::string appVersion;
          std::string userAgent;
          std::string platform;
          std::string language;
          std::string hardwareConcurrency;
          std::string DoNotTrack;
          std::vector<std::string> languages;
        };
        class timezone {
        public:
          timezone();
          ~timezone();

        public:
          const unsigned long long key = 128;
          bool enable = false;
        };
        class backup1 {
        public:
          backup1();
          ~backup1();

        public:
          const unsigned long long key = 256;
          bool enable = false;
        };
        class backup2 {
        public:
          backup2();
          ~backup2();

        public:
          const unsigned long long key = 2048;
          bool enable = false;
        };
        class random {
        public:
          random();
          ~random();

        public:
          const unsigned long long key = 512;
          bool enable = false;
        };
        class nocache {
        public:
          nocache();
          ~nocache();

        public:
          const unsigned long long key = 1024;
          bool enable = false;
        };

      public:
        canvas canvas_;
        webgl webgl_;
        audio audio_;
        font font_;
        clientrects clientrects_;
        webrtc webrtc_;
        navigator navigator_;
        timezone timezone_;
        random random_;
        nocache nocache_;
        backup1 backup1_;
        backup2 backup2_;
      };

    public:
      vs vs_;
    };

  public:
    f f_;
    std::string g_;
    std::string s_;
    unsigned long long v_;
    std::vector<std::string> b_;
    std::vector<std::string> w_;
  };
  class System {
  public:
    System();
    ~System();

  public:
    std::string fpset;
  };

private:
  void Init();
  void UnInit();
  std::string config_data_;

public:
  Fp fp;
  Account accout;
  std::map<unsigned int, Proxy> proxy_s;
  System system;
  std::string fp_json_string_;
  std::string fp_injector_script_string_;

public:
  std::string GenerateJavaScriptFp() const;
  std::string GetConfigFp() const;
  void Release() const;
};
static const char __default_script_template[] =
    R"((function(e){var t=JSON.parse(e);var n=t.g;var r=t.g.substr(1);var o=t.g.substr(2);if(window[n]){Object.assign(window[n],t)}else{window[n]=t}var a=function(e,n){return n.forEach((function(n){return t[n]=(e|t.v)==t.v?e:0}))};if(a(1,["toDataURL","toBlob","getImageData","measureText","convertToBlob"]),a(2,["readPixels","getExtension","getParameter"]),a(4,["getChannelData","getFloatFrequencyData"]),a(8,["offsetWidth","offsetHeight"]),a(16,["getClientRects"]),a(32,["enumerateDevices","MediaStreamTrack","RTCPeerConnection","RTCSessionDescription","webkitMediaStreamTrack","webkitRTCPeerConnection","webkitRTCSessionDescription"]),a(64,["getBattery","getGamepads","getVRDisplays","screen","platform","language","languages"]),a(128,["getTimezoneOffset","resolvedOptions"]),a(256,["logs"]),a(1024,["nocache"]),a(2048,["wlist"]),t.s=(512|t.v)==t.v?Math.random():parseFloat(t.s),!window[r]){var i=function(e,t,n){var r={};r[t]=n,window.dispatchEvent(new CustomEvent(e,{detail:r}))},c=function(e){return Math.floor(t.s*e)},u=function(){return t.i%t.c==0&&(t.i=1,t.n.push(t.c=t.n.shift())),t.r%t.c==t.i++?1:0};Object.assign(t,{i:0,c:7,n:[7,11,13,17,19,2053],r:c(1e6)});var s=function(e){if(e[r]){return e}var n=function(e,n){var r=arguments.length>2&&void 0!==arguments[2]?arguments[2]:void 0,o=function(n,r){var o=e[n];Object.defineProperty(e,n,{get:function(){return 0===t[n]&&o?o.bind(this):r}})};e&&("string"==typeof n?o(n,r):n instanceof Array?n.forEach((function(e){return o(e,r)})):Object.keys(n).forEach((function(e){return o(e,n[e])})))},a=function(e,n){return Object.keys(n).forEach((function(r){var o=e[r];Object.defineProperty(e,r,{get:function(){return 0!==t[r]?n[r]:o}})}))},s=function(n){try{n(e)}catch(e){t.debug&&console.error(e)}};return s((function(e){var r=e.WebGLRenderingContext.prototype,o=r.getParameter,a=r.getExtension,i=r.readPixels,u=null,s=null;n(e.WebGLRenderingContext.prototype,{readPixels:function(e,n,r,o,a,c,u,s){var p=t.s+1,l=Math.max(1,parseInt(r/11)),f=Math.max(1,parseInt(o/11));if(i.apply(this,arguments),u instanceof Uint8Array)for(var d=0;d<o;d+=f)for(var g=0;g<r;g+=l)u[4*r*d+4*g]*=p},getParameter:function(t){return t==e.WebGLRenderingContext.RENDERER||t==u?"Intel HD Graphics "+(c(8e3)+500)+" OpenGL Engine":t==e.WebGLRenderingContext.VENDOR||t==s?"Google Inc.":o.apply(this,arguments)},getExtension:function(e){var t=a.apply(this,arguments);return"WEBGL_debug_renderer_info"==e&&(u=t.UNMASKED_RENDERER_WEBGL,s=t.UNMASKED_VENDOR_WEBGL),t}})})),s((function(e){var r=e.WebGL2RenderingContext.prototype,o=r.getParameter,a=r.getExtension,i=r.readPixels,u=null,s=null;n(e.WebGL2RenderingContext.prototype,{readPixels:function(e,n,r,o,a,c,u,s){var p=t.s+1,l=Math.max(1,parseInt(r/11)),f=Math.max(1,parseInt(o/11));if(i.apply(this,arguments),u instanceof Uint8Array)for(var d=0;d<o;d+=f)for(var g=0;g<r;g+=l)u[4*r*d+4*g]*=p},getParameter:function(t){return t==e.WebGL2RenderingContext.RENDERER||t==u?"Intel HD Graphics "+(c(8e3)+500)+" OpenGL Engine":t==e.WebGL2RenderingContext.VENDOR||t==s?"Google Inc.":o.apply(this,arguments)},getExtension:function(e){var t=a.apply(this,arguments);return"WEBGL_debug_renderer_info"==e&&(u=t.UNMASKED_RENDERER_WEBGL,s=t.UNMASKED_VENDOR_WEBGL),t}})})),s((function(e){var r=e.CanvasRenderingContext2D.prototype,a=r.measureText,c=r.getImageData;n(e.CanvasRenderingContext2D.prototype,{measureText:function(){var n=a.apply(this,arguments),r=n.actualBoundingBoxAscent,o=n.actualBoundingBoxDescent,i=n.actualBoundingBoxLeft,c=n.actualBoundingBoxRight,u=n.fontBoundingBoxAscent,s=n.fontBoundingBoxDescent,p=n.width;return p+=t.s/1e6,{__proto__:e.TextMetrics.prototype,actualBoundingBoxAscent:r,actualBoundingBoxDescent:o,actualBoundingBoxLeft:i,actualBoundingBoxRight:c,fontBoundingBoxAscent:u,fontBoundingBoxDescent:s,width:p}},getImageData:function(e,n,r,o,a){for(var i=t.s+1,u=Math.max(1,parseInt(r/11)),s=Math.max(1,parseInt(o/11)),p=c.apply(this,arguments),l=0;l<o;l+=s)for(var f=0;f<r;f+=u)p.data[4*r*l+4*f]*=i;return p}});var u=e.HTMLCanvasElement.prototype,s=u.toDataURL,p=u.toBlob,l=function(e){if(e[o])return e[o];t.logs&&i("clogs",location.host,s.apply(e)),e[o]=e.cloneNode(!0);var n=e.getContext("2d").getImageData(0,0,e.width,e.height);return e[o].getContext("2d").putImageData(n,0,0),e[o]};n(e.HTMLCanvasElement.prototype,{toDataURL:function(){return s.apply(l(this),arguments)},toBlob:function(){return p.apply(l(this),arguments)}});var f=e.OffscreenCanvas.prototype.convertToBlob,d=function(t){if(t[o])return t[o];var n=e.document.createElement("canvas");n.width=t.width,n.height=t.height;var r=n.getContext("2d");r.drawImage(t.transferToImageBitmap(),0,0);var a=r.getImageData(0,0,n.width,n.height);return t[o]=new e.OffscreenCanvas(t.width,t.height),t[o].getContext("2d").putImageData(a,0,0),t[o]};n(e.OffscreenCanvas.prototype,{convertToBlob:function(){return f.apply(d(this),arguments)}})})),s((function(e){var r=null,o=e.AudioBuffer.prototype.getChannelData;n(e.AudioBuffer.prototype,{getChannelData:function(){var e=o.apply(this,arguments);if(r==e)return r;r=e;for(var n=0;n<r.length;n+=88){var a=c(n);r[a]=(r[a]+t.s)/2}return r}})})),s((function(e){var r=e.AnalyserNode.prototype.getFloatFrequencyData;n(e.AnalyserNode.prototype,{getFloatFrequencyData:function(){for(var e=r.apply(this,arguments),n=0;n<arguments[0].length;n+=88){var o=c(n);arguments[o]=(arguments[o]+t.s)/2}return e}})})),s((function(e){return n=e.HTMLElement.prototype,r={offsetWidth:function(){return Math.floor(this.getBoundingClientRect().width)+u()},offsetHeight:function(){return Math.floor(this.getBoundingClientRect().height)+u()}},Object.keys(r).forEach((function(e){var o=n.__lookupGetter__(e);Object.defineProperty(n,e,{get:function(){return(0!==t[e]?r[e]:o).apply(this,arguments)}})}));var n,r})),s((function(e){return n(e.Element.prototype,"getClientRects",(function(){return{0:{x:0,y:0,top:0,bottom:c(500),left:0,right:c(400),height:c(500),width:c(400),__proto__:e.DOMRect.prototype},length:1,__proto__:e.DOMRectList.prototype}}))})),s((function(e){return a(e,{screen:{availLeft:t.fps.screen.availLeft,availTop:t.fps.screen.availTop,availWidth:t.fps.screen.availWidth,availHeight:t.fps.screen.availHeight,width:t.fps.screen.width,height:t.fps.screen.height,colorDepth:t.fps.screen.colorDepth,pixelDepth:t.fps.screen.pixelDepth,__proto__:e.Screen.prototype,orientation:{angle:0,type:"landscape-primary",onchange:null,__proto__:e.ScreenOrientation.prototype}}})})),s((function(e){return n(e.navigator,["getBattery","getGamepads","getVRDisplays"])})),s((function(e){return a(e.navigator,{appCodeName:t.fps.navigator.appCodeName,appName:t.fps.navigator.appName,appVersion:t.fps.navigator.appVersion,userAgent:t.fps.navigator.userAgent,platform:t.fps.navigator.platform,language:t.fps.navigator.language,languages:t.fps.navigator.languages,hardwareConcurrency:"8"})})),s((function(e){var t=e.navigator.mediaDevices.enumerateDevices;n(e.navigator.mediaDevices,{enumerateDevices:function(){return t.apply(this,arguments).then((function(t){return t.push({deviceId:"default",groupId:"n/a",kind:"audiooutput",label:"FPS-Audio "+c(400),__proto__:e.MediaDeviceInfo.prototype}),t}))}})})),s((function(e){return n(e,["MediaStreamTrack","RTCPeerConnection","RTCSessionDescription","webkitMediaStreamTrack","webkitRTCPeerConnection","webkitRTCSessionDescription"])})),s((function(e){return n(e.Intl.DateTimeFormat.prototype,"resolvedOptions",(function(){return{calendar:"gregory",day:"numeric",locale:"en-US",month:"numeric",numberingSystem:"latn",timeZone:"UTC",year:"numeric"}}))})),s((function(e){return n(e.Date.prototype,"getTimezoneOffset",(function(){return[720,660,600,570,540,480,420,360,300,240,210,180,120,60,0,-60,-120,-180,-210,-240,-270,-300,-330,-345,-360,-390,-420,-480,-510,-525,-540,-570,-600,-630,-660,-720,-765,-780,-840][c(39)]}))})),e[r]=!0,e};s(window);var p=window.HTMLIFrameElement.prototype.__lookupGetter__("contentWindow");var l=window.HTMLIFrameElement.prototype.__lookupGetter__("contentDocument");Object.defineProperties(window.HTMLIFrameElement.prototype,{contentWindow:{get:function(){try{return s(p.apply(this,arguments))}catch(e){return p.apply(this,arguments)}}},contentDocument:{get:function(){try{s(p.apply(this,arguments))}finally{return l.apply(this,arguments)}}}}),t.nocache&&i("ncache","origin",location.origin)}})('%s'))";

Configure::Configure(const char *config, size_t configLen) {
  config_data_.append(config, configLen);
  Init();
}
Configure::~Configure() {
  UnInit();
}
Configure::System::System() {
}
Configure::System::~System() {
}
Configure::Account::Account() {
}
Configure::Account::~Account() {
}
Configure::Proxy::Proxy() {
}
Configure::Proxy::Proxy(const Configure::Proxy &) {
}
Configure::Proxy::~Proxy() {
}
void Configure::Proxy::operator=(const Proxy &) {
}
bool Configure::Proxy::operator<(const Proxy &) {
  return false;
}
bool Configure::Proxy::operator>(const Proxy &) {
  return false;
}
bool Configure::Proxy::operator==(const Proxy &) {
  return false;
}
Configure::Fp::Fp() {
}
Configure::Fp::~Fp() {
}
Configure::Fp::f::f() {
}
Configure::Fp::f::~f() {
}
Configure::Fp::f::vs::vs() {
}
Configure::Fp::f::vs::~vs() {
}
Configure::Fp::f::vs::canvas::canvas() {
}
Configure::Fp::f::vs::canvas::~canvas() {
}
Configure::Fp::f::vs::webrtc::webrtc() {
}
Configure::Fp::f::vs::webrtc::~webrtc() {
}
Configure::Fp::f::vs::webgl::webgl() {
}
Configure::Fp::f::vs::webgl::~webgl() {
}
Configure::Fp::f::vs::audio::audio() {
}
Configure::Fp::f::vs::audio::~audio() {
}
Configure::Fp::f::vs::font::font() {
}
Configure::Fp::f::vs::font::~font() {
}
Configure::Fp::f::vs::clientrects::clientrects() {
}
Configure::Fp::f::vs::clientrects::~clientrects() {
}
Configure::Fp::f::vs::clientrects::screen::screen() {
}
Configure::Fp::f::vs::clientrects::screen::~screen() {
}
Configure::Fp::f::vs::navigator::navigator() {
}
Configure::Fp::f::vs::navigator::~navigator() {
}
Configure::Fp::f::vs::timezone::timezone() {
}
Configure::Fp::f::vs::timezone::~timezone() {
}
Configure::Fp::f::vs::backup1::backup1() {
}
Configure::Fp::f::vs::backup1::~backup1() {
}
Configure::Fp::f::vs::backup2::backup2() {
}
Configure::Fp::f::vs::backup2::~backup2() {
}
Configure::Fp::f::vs::random::random() {
}
Configure::Fp::f::vs::random::~random() {
}
Configure::Fp::f::vs::nocache::nocache() {
}
Configure::Fp::f::vs::nocache::~nocache() {
}

void Configure::Init() {
  do {
    if (config_data_.empty())
      break;
    rapidjson::Document doc;
    doc.Parse(config_data_.data(), config_data_.size());
    if (doc.HasParseError())
      break;
    if (!doc.IsObject())
      break;

    do { //!@ system
      if (!doc.HasMember("system"))
        break;
      if (!doc["system"].IsObject())
        break;

      if (doc["system"].HasMember("fpset") &&
          doc["system"]["fpset"].IsString()) {
        system.fpset = doc["system"]["fpset"].GetString();

        char *base64un = nullptr;
        size_t base64unLen = 0;
        if (SK_NO == sk_base64decode(system.fpset.data(), system.fpset.size(),
                                     &base64un, &base64unLen))
          break;
        system.fpset.clear();
        system.fpset.append(base64un, base64unLen);
        fp_injector_script_string_ = system.fpset;
        sk_free_s((void **)&base64un);
      }
    } while (0);

    do { //!@ account
      if (!doc.HasMember("account"))
        break;
      if (!doc["account"].IsObject())
        break;
      if (doc["account"].HasMember("user") &&
          doc["account"]["user"].IsString()) {
        accout.user = doc["account"]["user"].GetString();
      }
      if (doc["account"].HasMember("pwd") && doc["account"]["pwd"].IsString()) {
        accout.pwd = doc["account"]["pwd"].GetString();
      }
      if (doc["account"].HasMember("identify") &&
          doc["account"]["identify"].IsUint64()) {
        accout.identify = doc["account"]["identify"].GetUint64();
      }
      if (doc["account"].HasMember("level") &&
          doc["account"]["level"].IsNumber()) {
        accout.level = doc["account"]["level"].GetUint();
      }

    } while (0);

    do { //!@ proxy
      if (!doc.HasMember("proxy"))
        break;
      if (!doc["proxy"].IsArray())
        break;

      for (auto *it = doc["proxy"].Begin(); it != doc["proxy"].End(); ++it) {
        if (!it->IsObject())
          break;
        Configure::Proxy proxy;
        if (it->HasMember("type") && (*it)["type"].IsNumber()) {
          proxy.type = (*it)["type"].GetUint();
        }
        if (it->HasMember("addr") && (*it)["addr"].IsString()) {
          proxy.addr = (*it)["addr"].GetString();
        }
        if (it->HasMember("name") && (*it)["name"].IsString()) {
          proxy.name = (*it)["name"].GetString();
        }
        if (it->HasMember("pwd") && (*it)["pwd"].IsString()) {
          proxy.pwd = (*it)["pwd"].GetString();
        }
        if (it->HasMember("port") && (*it)["port"].IsNumber()) {
          proxy.port = (*it)["port"].GetUint();
        }
        auto exists = proxy_s.find(proxy.type);
        if (exists != proxy_s.end())
          proxy_s.erase(exists);
        proxy_s.emplace(proxy.type, proxy);
      }
    } while (0);

    do { //!@ fp
      if (!doc.HasMember("fp"))
        break;
      if (!doc["fp"].IsObject())
        break;
      if (doc["fp"].HasMember("g") && doc["fp"]["g"].IsString()) {
        fp.g_ = doc["fp"]["g"].GetString();
      }
      if (doc["fp"].HasMember("s") && doc["fp"]["s"].IsString()) {
        fp.s_ = doc["fp"]["s"].GetString();
      }
      if (doc["fp"].HasMember("v") && doc["fp"]["v"].IsNumber()) {
        fp.v_ = doc["fp"]["v"].GetUint64();
      }
      if (doc["fp"].HasMember("w") && doc["fp"]["w"].IsArray()) {
        for (auto *it = doc["fp"]["w"].Begin(); it != doc["fp"]["w"].End();
             ++it) {
          if (!it->IsString())
            break;
          fp.w_.push_back(it->GetString());
        }
      }
      if (doc["fp"].HasMember("b") && doc["fp"]["b"].IsArray()) {
        for (auto *it = doc["fp"]["b"].Begin(); it != doc["fp"]["b"].End();
             ++it) {
          if (!it->IsString())
            break;
          fp.b_.push_back(it->GetString());
        }
      }

      if (doc["fp"].HasMember("f") && doc["fp"]["f"].IsObject()) {
        if (!doc["fp"]["f"].HasMember("vs") && doc["fp"]["f"]["vs"].IsObject())
          break;
        if (doc["fp"]["f"]["vs"].HasMember("canvas") &&
            doc["fp"]["f"]["vs"]["canvas"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["canvas"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["canvas"]["enable"].IsBool()) {
            fp.f_.vs_.canvas_.enable =
                doc["fp"]["f"]["vs"]["canvas"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("webgl") &&
            doc["fp"]["f"]["vs"]["webgl"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["webgl"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["webgl"]["enable"].IsBool()) {
            fp.f_.vs_.webgl_.enable =
                doc["fp"]["f"]["vs"]["webgl"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("audio") &&
            doc["fp"]["f"]["vs"]["audio"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["audio"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["audio"]["enable"].IsBool()) {
            fp.f_.vs_.audio_.enable =
                doc["fp"]["f"]["vs"]["audio"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("font") &&
            doc["fp"]["f"]["vs"]["font"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["font"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["font"]["enable"].IsBool()) {
            fp.f_.vs_.font_.enable =
                doc["fp"]["f"]["vs"]["font"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("webrtc") &&
            doc["fp"]["f"]["vs"]["webrtc"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["webrtc"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["webrtc"]["enable"].IsBool()) {
            fp.f_.vs_.webrtc_.enable =
                doc["fp"]["f"]["vs"]["webrtc"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("timezone") &&
            doc["fp"]["f"]["vs"]["timezone"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["timezone"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["timezone"]["enable"].IsBool()) {
            fp.f_.vs_.timezone_.enable =
                doc["fp"]["f"]["vs"]["timezone"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("random") &&
            doc["fp"]["f"]["vs"]["random"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["random"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["random"]["enable"].IsBool()) {
            fp.f_.vs_.random_.enable =
                doc["fp"]["f"]["vs"]["random"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("nocache") &&
            doc["fp"]["f"]["vs"]["nocache"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["nocache"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["nocache"]["enable"].IsBool()) {
            fp.f_.vs_.nocache_.enable =
                doc["fp"]["f"]["vs"]["nocache"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("backup1") &&
            doc["fp"]["f"]["vs"]["backup1"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["backup1"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["backup1"]["enable"].IsBool()) {
            fp.f_.vs_.backup1_.enable =
                doc["fp"]["f"]["vs"]["backup1"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("backup2") &&
            doc["fp"]["f"]["vs"]["backup2"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["backup2"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["backup2"]["enable"].IsBool()) {
            fp.f_.vs_.backup2_.enable =
                doc["fp"]["f"]["vs"]["backup2"]["enable"].GetBool();
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("clientrects") &&
            doc["fp"]["f"]["vs"]["clientrects"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["clientrects"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["clientrects"]["enable"].IsBool()) {
            fp.f_.vs_.clientrects_.enable =
                doc["fp"]["f"]["vs"]["clientrects"]["enable"].GetBool();
          }
          if (doc["fp"]["f"]["vs"]["clientrects"].HasMember("screen") &&
              doc["fp"]["f"]["vs"]["clientrects"]["screen"].IsObject()) {
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "availLeft") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availLeft"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.availLeft =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availLeft"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "availTop") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availTop"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.availTop =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availTop"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "availWidth") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availWidth"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.availWidth =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availWidth"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "availHeight") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availHeight"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.availHeight =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["availHeight"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "width") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["width"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.width =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["width"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "height") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["height"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.height =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["height"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "colorDepth") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["colorDepth"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.colorDepth =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["colorDepth"]
                      .GetUint();
            }
            if (doc["fp"]["f"]["vs"]["clientrects"]["screen"].HasMember(
                    "pixelDepth") &&
                doc["fp"]["f"]["vs"]["clientrects"]["screen"]["pixelDepth"]
                    .IsNumber()) {
              fp.f_.vs_.clientrects_.screen_.pixelDepth =
                  doc["fp"]["f"]["vs"]["clientrects"]["screen"]["pixelDepth"]
                      .GetUint();
            }
          }
        }
        if (doc["fp"]["f"]["vs"].HasMember("navigator") &&
            doc["fp"]["f"]["vs"]["navigator"].IsObject()) {
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("enable") &&
              doc["fp"]["f"]["vs"]["navigator"]["enable"].IsBool()) {
            fp.f_.vs_.navigator_.enable =
                doc["fp"]["f"]["vs"]["navigator"]["enable"].GetBool();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("appCodeName") &&
              doc["fp"]["f"]["vs"]["navigator"]["appCodeName"].IsString()) {
            fp.f_.vs_.navigator_.appCodeName =
                doc["fp"]["f"]["vs"]["navigator"]["appCodeName"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("appName") &&
              doc["fp"]["f"]["vs"]["navigator"]["appName"].IsString()) {
            fp.f_.vs_.navigator_.appName =
                doc["fp"]["f"]["vs"]["navigator"]["appName"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("appVersion") &&
              doc["fp"]["f"]["vs"]["navigator"]["appVersion"].IsString()) {
            fp.f_.vs_.navigator_.appVersion =
                doc["fp"]["f"]["vs"]["navigator"]["appVersion"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("userAgent") &&
              doc["fp"]["f"]["vs"]["navigator"]["userAgent"].IsString()) {
            fp.f_.vs_.navigator_.userAgent =
                doc["fp"]["f"]["vs"]["navigator"]["userAgent"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("platform") &&
              doc["fp"]["f"]["vs"]["navigator"]["platform"].IsString()) {
            fp.f_.vs_.navigator_.platform =
                doc["fp"]["f"]["vs"]["navigator"]["platform"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("language") &&
              doc["fp"]["f"]["vs"]["navigator"]["language"].IsString()) {
            fp.f_.vs_.navigator_.language =
                doc["fp"]["f"]["vs"]["navigator"]["language"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember(
                  "hardwareConcurrency") &&
              doc["fp"]["f"]["vs"]["navigator"]["hardwareConcurrency"]
                  .IsString()) {
            fp.f_.vs_.navigator_.hardwareConcurrency =
                doc["fp"]["f"]["vs"]["navigator"]["hardwareConcurrency"]
                    .GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("DoNotTrack") &&
              doc["fp"]["f"]["vs"]["navigator"]["DoNotTrack"].IsString()) {
            fp.f_.vs_.navigator_.DoNotTrack =
                doc["fp"]["f"]["vs"]["navigator"]["DoNotTrack"].GetString();
          }
          if (doc["fp"]["f"]["vs"]["navigator"].HasMember("languages") &&
              doc["fp"]["f"]["vs"]["navigator"]["languages"].IsArray()) {
            for (auto *it =
                     doc["fp"]["f"]["vs"]["navigator"]["languages"].Begin();
                 it != doc["fp"]["f"]["vs"]["navigator"]["languages"].End();
                 ++it) {
              if (!it->IsString())
                break;
              fp.f_.vs_.navigator_.languages.push_back(it->GetString());
            }
          }
        }

        do {
          if (doc["fp"].HasMember("b"))
            doc["fp"].RemoveMember("b");
          if (doc["fp"].HasMember("w"))
            doc["fp"].RemoveMember("w");
          if (doc["fp"].HasMember("v"))
            doc["fp"].RemoveMember("v");

          std::string bstr;
          for (const auto &node : fp.b_) {
            if (node.empty())
              continue;
            bstr.append(node);
            bstr.append(",");
          }
          std::string wstr;
          for (const auto &node : fp.w_) {
            if (node.empty())
              continue;
            wstr.append(node);
            wstr.append(",");
          }
          doc["fp"].AddMember(
              rapidjson::Value::StringRefType("b"),
              rapidjson::Value::StringRefType(bstr.empty() ? "" : bstr.c_str()),
              doc.GetAllocator());
          doc["fp"].AddMember(
              rapidjson::Value::StringRefType("w"),
              rapidjson::Value::StringRefType(wstr.empty() ? "" : wstr.c_str()),
              doc.GetAllocator());

          unsigned long v_value = 0;
          if (fp.f_.vs_.audio_.enable) {
            v_value |= fp.f_.vs_.audio_.key;
          }
          if (fp.f_.vs_.backup1_.enable) {
            v_value |= fp.f_.vs_.backup1_.key;
          }
          if (fp.f_.vs_.backup2_.enable) {
            v_value |= fp.f_.vs_.backup2_.key;
          }
          if (fp.f_.vs_.canvas_.enable) {
            v_value |= fp.f_.vs_.canvas_.key;
          }
          if (fp.f_.vs_.clientrects_.enable) {
            v_value |= fp.f_.vs_.clientrects_.key;
          }
          if (fp.f_.vs_.font_.enable) {
            v_value |= fp.f_.vs_.font_.key;
          }
          if (fp.f_.vs_.navigator_.enable) {
            v_value |= fp.f_.vs_.navigator_.key;
          }
          if (fp.f_.vs_.nocache_.enable) {
            v_value |= fp.f_.vs_.nocache_.key;
          }
          if (fp.f_.vs_.random_.enable) {
            v_value |= fp.f_.vs_.random_.key;
          }
          if (fp.f_.vs_.webgl_.enable) {
            v_value |= fp.f_.vs_.webgl_.key;
          }
          if (fp.f_.vs_.timezone_.enable) {
            v_value |= fp.f_.vs_.timezone_.key;
          }
          if (fp.f_.vs_.webrtc_.enable) {
            v_value |= fp.f_.vs_.webrtc_.key;
          }
          rapidjson::Value vObj;
          vObj.SetUint(v_value);
          doc["fp"].AddMember(rapidjson::Value::StringRefType("v"), vObj,
                              doc.GetAllocator());

          rapidjson::StringBuffer strBuf;
          rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
          doc["fp"].Accept(writer);
          fp_json_string_ = strBuf.GetString();
        } while (0);
      }
    } while (0);
  } while (0);
}
void Configure::UnInit() {
}
void Configure::Release() const {
  delete this;
}
std::string Configure::GetConfigFp() const {
  return fp_json_string_;
}
std::string Configure::GenerateJavaScriptFp() const {
  return fp_injector_script_string_;
}
BASE_EXPORT void sk_configure_destroy(CONFIG_HANDLE *handle) {
  do {
    if (!*handle)
      break;
    Configure *pConfig = reinterpret_cast<Configure *>(*handle);
    pConfig->Release();
    *handle = nullptr;
  } while (0);
}
int sk_configure_injector_script_generate(CONFIG_HANDLE handle, char **dst,
                                          size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!handle)
      break;
    Configure *pConfig = reinterpret_cast<Configure *>(handle);
    std::string tmp1 = pConfig->fp_injector_script_string_;
    std::string tmp2 = pConfig->fp_json_string_;
    tmp1.replace(tmp1.find("%s"), 2, tmp2);
    *dstLen = tmp1.size();
    *dst = (char *)sk_malloc(*dstLen);
    memcpy(*dst, tmp1.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}

int sk_configure_injector_settings(CONFIG_HANDLE handle, char **dst,
                                   size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!handle)
      break;
    Configure *pConfig = reinterpret_cast<Configure *>(handle);
    std::string strConfig = pConfig->GetConfigFp();
    if (strConfig.empty())
      break;
    *dstLen = strConfig.size();
    *dst = (char *)sk_malloc(*dstLen);
    memcpy(*dst, strConfig.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
CONFIG_HANDLE sk_configure_create(const char *config_data,
                                  size_t config_dataLen) {
  CONFIG_HANDLE result = nullptr;
  do {
    if (!config_data || config_dataLen <= 0)
      break;
    rapidjson::Document doc;
    doc.Parse(config_data, config_dataLen);
    if (doc.HasParseError())
      break;
    if (!doc.IsObject())
      break;
    result = new Configure(config_data, config_dataLen);
  } while (0);
  return result;
}
int sk_verify_configure(const char *src, size_t srcLen, char **dst,
                        size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    rapidjson::Document doc;
    doc.Parse(src, srcLen);
    if (doc.HasParseError())
      break;
    if (!doc.IsObject())
      break;
    rapidjson::StringBuffer strBuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
    doc.Accept(writer);
    std::string strConfig = strBuf.GetString();
    *dstLen = strConfig.size();
    *dst = (char *)sk_malloc(*dstLen);
    memcpy(*dst, strConfig.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
size_t sk_get_default_injector_script_size() {
  return sizeof(__default_script_template);
}
void sk_get_default_injector_script(char **dst, size_t *dstLen) {
  *dst = NULL;
  *dstLen = 0;
  do {
    *dstLen = sizeof(__default_script_template);
    *dst = (char *)sk_malloc(*dstLen);
    if (!*dst)
      break;
    memcpy(*dst, __default_script_template, *dstLen);
  } while (0);
}
const char *sk_get_default_injector_script_encode() {
  return NULL;
}
} // namespace fps

int sk_get_temp_path(char **path, size_t *pathLen) {
  int result = SK_NO;
  *path = nullptr;
  *pathLen = 0;
  do {
    base::FilePath path_w;
    if (!base::GetTempDir(&path_w))
      break;
    std::string path_a = path_w.MaybeAsASCII();
    if (path_a.empty())
      break;
    *path = (char *)sk_malloc(path_a.size());
    if (!*path)
      break;
    memcpy(*path, path_a.data(), path_a.size());
    *pathLen = path_a.size();
    result = SK_OK;
  } while (0);
  return result;
}
int sk_get_current_process_path(char **path, size_t *pathLen) {
  int result = SK_NO;
  *path = nullptr;
  *pathLen = 0;
  do {
    base::FilePath executableDir;
    base::PathService::Get(base::DIR_EXE, &executableDir);
    std::string executableDirA = executableDir.MaybeAsASCII();
    *path = (char *)sk_malloc(executableDirA.size());
    if (!*path)
      break;
    memcpy(*path, executableDirA.data(), executableDirA.size());
    *pathLen = executableDirA.size();
    result = SK_OK;
  } while (0);
  return result;
}

int sk_read_file(const char *file_path, char **dst, size_t *dstLen) {
  int result = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!file_path)
      break;
    std::fstream of(file_path, static_cast<std::ios_base::openmode>(
                                   std::ios::in | std::ios::binary));
    if (!of.is_open())
      break;
    of.seekg(0, of.end);
    *dstLen = static_cast<size_t>(of.tellg());
    if (*dstLen <= 0)
      break;
    *dst = (char *)sk_malloc(*dstLen);
    of.seekg(0, of.beg);
    of.read(*dst, static_cast<std::streamsize>(*dstLen));
    of.close();
    result = SK_OK;
  } while (0);
  return result;
}
int sk_write_file(const char *file_path, const char *src, size_t srcLen) {
  int result = SK_NO;
  do {
    if (!file_path || !src || srcLen <= 0)
      break;
    std::fstream of(file_path, static_cast<std::ios_base::openmode>(
                                   static_cast<int>(std::ios::binary) |
                                   static_cast<int>(std::ios::out) |
                                   static_cast<int>(std::ios::trunc)));
    if (!of.is_open())
      break;
    of << std::string(src, srcLen);
    of.close();
    result = SK_OK;
  } while (0);
  return result;
}
int sk_write_file_addto(const char *file_path, const char *src, size_t srcLen) {
  int result = SK_NO;
  do {
    if (!file_path || !src || srcLen <= 0)
      break;
    std::ofstream of(file_path,
                     std::ios::binary | std::ios::out | std::ios::app);
    if (!of.is_open())
      break;
    of << std::string(src, srcLen);
    of.close();
    result = SK_OK;
  } while (0);
  return result;
}

void sk_ipc_message_destroy(IPC_MSG **msg) {
  do {
    if (!*msg)
      break;
    sk_free_s((void **)msg);
  } while (0);
}
int sk_ipc_message_create(int msgType, const char *src, size_t srcLen,
                          char **dst, size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!(msgType >= (int)IPC_MSGTYPE::IPC_MSGTYPE_BEGIN &&
          msgType <= (int)IPC_MSGTYPE::IPC_MSGTYPE_END))
      break;
    if (!src || srcLen <= 0)
      break;
    size_t tmpBUfferLen = srcLen + sizeof(IPC_MSG) - sizeof(char);
    char *tmpBuffer = (char *)sk_malloc(tmpBUfferLen);
    if (!tmpBuffer)
      break;
    ((IPC_MSG *)tmpBuffer)->type = msgType;
    ((IPC_MSG *)tmpBuffer)->head = IPC_MSG_HEAD;
    ((IPC_MSG *)tmpBuffer)->tail = IPC_MSG_TAIL;
    ((IPC_MSG *)tmpBuffer)->len = srcLen;
    memcpy(((IPC_MSG *)tmpBuffer)->data, src, srcLen);

    /*
const unsigned long long LOGO_OF_DEVELOPER_BEGIN = 0xFAC9C2D0;
const unsigned long long LOGO_OF_DEVELOPER_END = 0xB4B4AAC1;
*/

    char *srcBase64 = nullptr;
    size_t srcBase64Len = 0;
    sk_base64encode(tmpBuffer, tmpBUfferLen, &srcBase64, &srcBase64Len);
    if (!srcBase64 || srcBase64Len <= 0)
      break;
    sk_free_s((void **)&tmpBuffer);
    *dstLen = srcBase64Len + sizeof(unsigned long long) * 2;
    *dst = (char *)sk_malloc(*dstLen);
    (*dst)[0] = '#';
    memcpy(*dst, (char *)&LOGO_OF_DEVELOPER_BEGIN,
           sizeof(LOGO_OF_DEVELOPER_BEGIN));
    memcpy(*dst + srcBase64Len + sizeof(LOGO_OF_DEVELOPER_BEGIN),
           (char *)&LOGO_OF_DEVELOPER_END, sizeof(LOGO_OF_DEVELOPER_END));
    memcpy((*dst) + sizeof(LOGO_OF_DEVELOPER_BEGIN), srcBase64, srcBase64Len);
    sk_free_s((void **)&srcBase64);
    result = SK_OK;
  } while (0);
  return result;
}
int sk_ipc_message_parser(const char *data, size_t dataLen, IPC_MSG **msg) {
  int result = SK_NO;
  *msg = nullptr;
  do {
    if (!data || dataLen <= 0)
      break;
    size_t begin = 0;
    size_t end = 0;
    for (size_t i = 0; i < dataLen - sizeof(LOGO_OF_DEVELOPER_BEGIN); ++i) {
      if (memcmp((const void *)&data[i], (const void *)&LOGO_OF_DEVELOPER_BEGIN,
                 sizeof(&LOGO_OF_DEVELOPER_BEGIN)) == 0)
        break;
      begin++;
    }
    for (size_t i = 0; i < dataLen - sizeof(LOGO_OF_DEVELOPER_END); ++i) {
      if (memcmp((const void *)&data[i], (const void *)&LOGO_OF_DEVELOPER_END,
                 sizeof(&LOGO_OF_DEVELOPER_END)) == 0)
        break;
      end++;
    }
    if (end <= begin)
      break;
    char *base64un = nullptr;
    size_t base64unLen = 0;
    sk_base64decode(data + begin + sizeof(&LOGO_OF_DEVELOPER_BEGIN),
                    end - begin - sizeof(&LOGO_OF_DEVELOPER_END), &base64un,
                    &base64unLen);
    if (!base64un || base64unLen <= 0)
      break;
    *msg = (IPC_MSG *)base64un;
    if ((*msg)->head != IPC_MSG_HEAD || (*msg)->tail != IPC_MSG_TAIL)
      break;
    result = SK_OK;
  } while (0);
  return result;
}
int sk_set_google_apit_key(const char *GOOGLE_API_KEY,
                           const char *GOOGLE_DEFAULT_CLIENT_ID,
                           const char *GOOGLE_DEFAULT_CLIENT_SECRET) {
  /*
 setx GOOGLE_API_KEY AIzaSyCkfPOPZXDKNn8hhgu3JrA62wIgC93d44k
 setx GOOGLE_DEFAULT_CLIENT_ID 811574891467.apps.googleusercontent.com
 setx GOOGLE_DEFAULT_CLIENT_SECRET kdloedMFGdGla2P1zacGjAQh
 */
  int result = SK_NO;
  do {
    if (!GOOGLE_API_KEY || !GOOGLE_DEFAULT_CLIENT_ID ||
        !GOOGLE_DEFAULT_CLIENT_SECRET) {
      putenv((char *)"GOOGLE_API_KEY=no");
      putenv((char *)"GOOGLE_DEFAULT_CLIENT_ID=no");
      putenv((char *)"GOOGLE_DEFAULT_CLIENT_SECRET=no");
      break;
    }

    char szTemp[1024] = {0};
    sprintf(szTemp, "GOOGLE_API_KEY=%s", GOOGLE_API_KEY);
    putenv(szTemp);
    sprintf(szTemp, "GOOGLE_DEFAULT_CLIENT_ID=%s", GOOGLE_DEFAULT_CLIENT_ID);
    putenv(szTemp);
    sprintf(szTemp, "GOOGLE_DEFAULT_CLIENT_SECRET=%s",
            GOOGLE_DEFAULT_CLIENT_SECRET);
    putenv(szTemp);

    result = SK_OK;
  } while (0);
  return result;
}

void sk_on_chromium_startup() {
  do {
    base::sk::sk_set_google_apit_key("AIzaSyCkfPOPZXDKNn8hhgu3JrA62wIgC93d44k",
                                     "811574891467.apps.googleusercontent.com",
                                     "kdloedMFGdGla2P1zacGjAQh");

  } while (0);
}
void sk_on_chromium_shutdown() {
  do {
  } while (0);
}
void sk_on_chromium_extensions_installer(load_chromium_extensions_cb load_cb,
                                         void *route) {
  char *curproc_path = nullptr;
  size_t curproc_path_size = 0;
  sk_get_current_process_path(&curproc_path, &curproc_path_size);
  std::string strCurrProcPath(curproc_path, curproc_path_size);
  strCurrProcPath.append("\\homepage");
  std::u16string utf16str = base::ASCIIToUTF16(StringPiece(strCurrProcPath));
  std::wstring wstrCurrProcPath = UTF16ToWide(StringPiece16(utf16str));

  char *manifest_data_buffer = nullptr;
  size_t manifest_data_bufferLen = 0;

  sk_read_file((strCurrProcPath + "\\manifest.json").c_str(),
               &manifest_data_buffer, &manifest_data_bufferLen);

  std::string strManifest(manifest_data_buffer, manifest_data_bufferLen);
  load_cb(strManifest.c_str(), wstrCurrProcPath.c_str(), route);

  sk_free_s((void **)&curproc_path);
  sk_free_s((void **)&manifest_data_buffer);
}

namespace conversions {
int wide_to_ascii(const wchar_t *src, size_t srcLen, char **dst,
                  size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::u16string u16str;
    if (!WideToUTF16(src, srcLen, &u16str))
      break;
    std::string str = UTF16ToASCII(StringPiece16(u16str));
    if (str.empty())
      break;
    *dstLen = str.size();
    *dst = (char *)sk_malloc(*dstLen);
    mempcy(*dst, str.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
int ascii_to_wide(const char *src, size_t srcLen, wchar_t **dst,
                  size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::string str(src, srcLen);
    std::u16string u16str = base::ASCIIToUTF16(StringPiece(str));
    std::wstring wstr = base::UTF16ToWide(StringPiece16(u16str));
    if (wstr.empty())
      break;
    *dstLen = wstr.size();
    *dst = (char *)sk_malloc((*dstLen) * sizeof(char));
    memcpy(*dst, (char *)wstr.data(), (*dstLen) * sizeof(char));
    result = SK_OK;
  } while (0);
  return result;
}
int utf8_to_wide(const char *src, size_t srcLen, wchar_t **dst,
                 size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::string u8str(src, srcLen);
    std::wstring wstr = base::UTF8ToWide(StringPiece(u8str));
    if (wstr.empty())
      break;
    *dstLen = wstr.size();
    *dst = (char *)sk_malloc((*dstLen) * sizeof(char));
    memcpy(*dst, (char *)wstr.data(), (*dstLen) * sizeof(char));
    result = SK_OK;
  } while (0);
  return result;
}
int wide_to_utf8(const wchar_t *src, size_t srcLen, char **dst,
                 size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::wstring wstr(src, srcLen);
    std::string str = WideToUTF8(WStringPiece(wstr));
    if (str.empty())
      break;
    *dstLen = str.size();
    *dst = sk_malloc(*dstLen);
    memcpy(*dst, str.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
int utf8_to_ascii(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::string u8str(src, srcLen);
    std::u16string u16str = base::UTF8ToUTF16(StringPiece(u8str));
    if (u16str.empty())
      break;
    std::string str = base::UTF16ToASCII(StringPiece16(u16str));
    if (str.empty())
      break;
    *dstLen = str.size();
    *dst = (char *)sk_malloc(*dstLen);
    memcpy(*dst, str.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
int ascii_to_utf8(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int result = SK_NO;
  *dst = nullptr;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    std::string str(src, srcLen);
    std::u16string u16str = base::ASCIIToUTF16(StringPiece(str));
    if (u16str.empty())
      break;
    std::string u8str = base::UTF16ToUTF8(StringPiece16(u16str));
    if (u8str.empty())
      break;
    *dstLen = u8str.size();
    *dst = (char *)sk_malloc(*dstLen);
    memcpy(*dst, u8str.data(), *dstLen);
    result = SK_OK;
  } while (0);
  return result;
}
} // namespace conversions

} // namespace sk

} // namespace base