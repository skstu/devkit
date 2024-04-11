!(function (e) {
  var t = {};
  function n(r) {
    if (t[r]) return t[r].exports;
    var o = (t[r] = {
      i: r,
      l: !1,
      exports: {},
    });
    return e[r].call(o.exports, o, o.exports, n), (o.l = !0), o.exports;
  }
  (n.m = e),
    (n.c = t),
    (n.d = function (e, t, r) {
      n.o(e, t) ||
        Object.defineProperty(e, t, {
          enumerable: !0,
          get: r,
        });
    }),
    (n.r = function (e) {
      "undefined" != typeof Symbol &&
        Symbol.toStringTag &&
        Object.defineProperty(e, Symbol.toStringTag, {
          value: "Module",
        }),
        Object.defineProperty(e, "__esModule", {
          value: !0,
        });
    }),
    (n.t = function (e, t) {
      if ((1 & t && (e = n(e)), 8 & t)) return e;
      if (4 & t && "object" == typeof e && e && e.__esModule) return e;
      var r = Object.create(null);
      if (
        (n.r(r),
        Object.defineProperty(r, "default", {
          enumerable: !0,
          value: e,
        }),
        2 & t && "string" != typeof e)
      )
        for (var o in e)
          n.d(
            r,
            o,
            function (t) {
              return e[t];
            }.bind(null, o)
          );
      return r;
    }),
    (n.n = function (e) {
      var t =
        e && e.__esModule
          ? function () {
              return e.default;
            }
          : function () {
              return e;
            };
      return n.d(t, "a", t), t;
    }),
    (n.o = function (e, t) {
      return Object.prototype.hasOwnProperty.call(e, t);
    }),
    (n.p = ""),
    n((n.s = 0));
})([
  function (e, t) {
    var n = function (e) {
      var t = JSON.parse(e),
        n = t.g,
        r = t.g.substr(1),
        o = t.g.substr(2);
      window[n] ? (t = Object.assign(window[n], t)) : (window[n] = t);
      var a = function (e, n) {
        return n.forEach(function (n) {
          return (t[n] = (e | t.v) == t.v ? e : 0);
        });
      };
      //if (a(1, ["toDataURL", "toBlob", "getImageData", "measureText", "convertToBlob"]), a(2, ["readPixels", "getExtension", "getParameter"]), a(4, ["getChannelData", "getFloatFrequencyData"]), a(8, ["offsetWidth", "offsetHeight"]), a(16, ["getClientRects"]), a(32, ["enumerateDevices", "MediaStreamTrack", "RTCPeerConnection", "RTCSessionDescription", "webkitMediaStreamTrack", "webkitRTCPeerConnection", "webkitRTCSessionDescription"]), a(64, ["getBattery", "getGamepads", "getVRDisplays", "screen", "platform", "language", "languages"]), a(128, ["getTimezoneOffset", "resolvedOptions"]), a(256, ["logs"]), a(1024, ["nocache"]), a(2048, ["wlist"]), t.s = (512 | t.v) == t.v ? Math.random() : parseFloat(t.s), !window[r]) {
      if (
        (a(1, [
          "toDataURL",
          "toBlob",
          "getImageData",
          "measureText",
          "convertToBlob",
        ]),
        a(2, ["readPixels", "getExtension", "getParameter"]),
        a(4, ["getChannelData", "getFloatFrequencyData"]),
        a(8, ["offsetWidth", "offsetHeight"]),
        a(16, ["getClientRects"]),
        a(32, [
          "enumerateDevices",
          "MediaStreamTrack",
          "RTCPeerConnection",
          "RTCSessionDescription",
          "webkitMediaStreamTrack",
          "webkitRTCPeerConnection",
          "webkitRTCSessionDescription",
        ]),
        a(64, [
          "getBattery",
          "getGamepads",
          "getVRDisplays",
          "screen",
          "platform",
          "language",
          "languages",
        ]),
        a(128, ["getTimezoneOffset", "resolvedOptions"]),
        a(256, ["logs"]),
        a(1024, ["nocache"]),
        a(2048, ["wlist"]),
        (t.s = Math.random()),
        !window[r])
      ) {
        var i = function (e, t, n) {
            var r = {};
            (r[t] = n),
              window.dispatchEvent(
                new CustomEvent(e, {
                  detail: r,
                })
              );
          },
          c = function (e) {
            return Math.floor(t.s * e);
          },
          u = function () {
            return (
              t.i % t.c == 0 && ((t.i = 1), t.n.push((t.c = t.n.shift()))),
              t.r % t.c == t.i++ ? 1 : 0
            );
          };
        Object.assign(t, {
          i: 0,
          c: 7,
          n: [7, 11, 13, 17, 19, 2053],
          r: c(1e6),
        });
        var s = function (e) {
          if (e[r]) return e;
          var n = function (e, n) {
              var r =
                  arguments.length > 2 && void 0 !== arguments[2]
                    ? arguments[2]
                    : void 0,
                o = function (n, r) {
                  var o = e[n];
                  Object.defineProperty(e, n, {
                    get: function () {
                      return 0 === t[n] && o ? o.bind(this) : r;
                    },
                  });
                };
              e &&
                ("string" == typeof n
                  ? o(n, r)
                  : n instanceof Array
                  ? n.forEach(function (e) {
                      return o(e, r);
                    })
                  : Object.keys(n).forEach(function (e) {
                      return o(e, n[e]);
                    }));
            },
            a = function (e, n) {
              return Object.keys(n).forEach(function (r) {
                var o = e[r];
                Object.defineProperty(e, r, {
                  get: function () {
                    return 0 !== t[r] ? n[r] : o;
                  },
                });
              });
            },
            s = function (n) {
              try {
                n(e);
              } catch (e) {
                t.debug && console.error(e);
              }
            };
          return (
            s(function (e) {
              var r = e.WebGLRenderingContext.prototype,
                o = r.getParameter,
                a = r.getExtension,
                i = r.readPixels,
                u = null,
                s = null;
              n(e.WebGLRenderingContext.prototype, {
                readPixels: function (e, n, r, o, a, c, u, s) {
                  var p = t.s + 1,
                    l = Math.max(1, parseInt(r / 11)),
                    f = Math.max(1, parseInt(o / 11));
                  if ((i.apply(this, arguments), u instanceof Uint8Array))
                    for (var d = 0; d < o; d += f)
                      for (var g = 0; g < r; g += l) u[4 * r * d + 4 * g] *= p;
                },
                getParameter: function (t) {
                  return t == e.WebGLRenderingContext.RENDERER || t == u
                    ? "Intel HD Graphics " + (c(8e3) + 500) + " OpenGL Engine"
                    : t == e.WebGLRenderingContext.VENDOR || t == s
                    ? "Google Inc."
                    : o.apply(this, arguments);
                },
                getExtension: function (e) {
                  var t = a.apply(this, arguments);
                  return (
                    "WEBGL_debug_renderer_info" == e &&
                      ((u = t.UNMASKED_RENDERER_WEBGL),
                      (s = t.UNMASKED_VENDOR_WEBGL)),
                    t
                  );
                },
              });
            }),
            s(function (e) {
              var r = e.WebGL2RenderingContext.prototype,
                o = r.getParameter,
                a = r.getExtension,
                i = r.readPixels,
                u = null,
                s = null;
              n(e.WebGL2RenderingContext.prototype, {
                readPixels: function (e, n, r, o, a, c, u, s) {
                  var p = t.s + 1,
                    l = Math.max(1, parseInt(r / 11)),
                    f = Math.max(1, parseInt(o / 11));
                  if ((i.apply(this, arguments), u instanceof Uint8Array))
                    for (var d = 0; d < o; d += f)
                      for (var g = 0; g < r; g += l) u[4 * r * d + 4 * g] *= p;
                },
                getParameter: function (t) {
                  return t == e.WebGL2RenderingContext.RENDERER || t == u
                    ? "Intel HD Graphics " + (c(8e3) + 500) + " OpenGL Engine"
                    : t == e.WebGL2RenderingContext.VENDOR || t == s
                    ? "Google Inc."
                    : o.apply(this, arguments);
                },
                getExtension: function (e) {
                  var t = a.apply(this, arguments);
                  return (
                    "WEBGL_debug_renderer_info" == e &&
                      ((u = t.UNMASKED_RENDERER_WEBGL),
                      (s = t.UNMASKED_VENDOR_WEBGL)),
                    t
                  );
                },
              });
            }),
            s(function (e) {
              var r = e.CanvasRenderingContext2D.prototype,
                a = r.measureText,
                c = r.getImageData;
              n(e.CanvasRenderingContext2D.prototype, {
                measureText: function () {
                  var n = a.apply(this, arguments),
                    r = n.actualBoundingBoxAscent,
                    o = n.actualBoundingBoxDescent,
                    i = n.actualBoundingBoxLeft,
                    c = n.actualBoundingBoxRight,
                    u = n.fontBoundingBoxAscent,
                    s = n.fontBoundingBoxDescent,
                    p = n.width;
                  return (
                    (p += t.s / 1e6),
                    {
                      __proto__: e.TextMetrics.prototype,
                      actualBoundingBoxAscent: r,
                      actualBoundingBoxDescent: o,
                      actualBoundingBoxLeft: i,
                      actualBoundingBoxRight: c,
                      fontBoundingBoxAscent: u,
                      fontBoundingBoxDescent: s,
                      width: p,
                    }
                  );
                },
                getImageData: function (e, n, r, o, a) {
                  for (
                    var i = t.s + 1,
                      u = Math.max(1, parseInt(r / 11)),
                      s = Math.max(1, parseInt(o / 11)),
                      p = c.apply(this, arguments),
                      l = 0;
                    l < o;
                    l += s
                  )
                    for (var f = 0; f < r; f += u)
                      p.data[4 * r * l + 4 * f] *= i;
                  return p;
                },
              });
              var u = e.HTMLCanvasElement.prototype,
                s = u.toDataURL,
                p = u.toBlob,
                l = function (e) {
                  if (e[o]) return e[o];
                  t.logs && i("clogs", location.host, s.apply(e)),
                    (e[o] = e.cloneNode(!0));
                  var n = e
                    .getContext("2d")
                    .getImageData(0, 0, e.width, e.height);
                  return e[o].getContext("2d").putImageData(n, 0, 0), e[o];
                };
              n(e.HTMLCanvasElement.prototype, {
                toDataURL: function () {
                  return s.apply(l(this), arguments);
                },
                toBlob: function () {
                  return p.apply(l(this), arguments);
                },
              });
              var f = e.OffscreenCanvas.prototype.convertToBlob,
                d = function (t) {
                  if (t[o]) return t[o];
                  var n = e.document.createElement("canvas");
                  (n.width = t.width), (n.height = t.height);
                  var r = n.getContext("2d");
                  r.drawImage(t.transferToImageBitmap(), 0, 0);
                  var a = r.getImageData(0, 0, n.width, n.height);
                  return (
                    (t[o] = new e.OffscreenCanvas(t.width, t.height)),
                    t[o].getContext("2d").putImageData(a, 0, 0),
                    t[o]
                  );
                };
              n(e.OffscreenCanvas.prototype, {
                convertToBlob: function () {
                  return f.apply(d(this), arguments);
                },
              });
            }),
            s(function (e) {
              var r = null,
                o = e.AudioBuffer.prototype.getChannelData;
              n(e.AudioBuffer.prototype, {
                getChannelData: function () {
                  var e = o.apply(this, arguments);
                  if (r == e) return r;
                  r = e;
                  for (var n = 0; n < r.length; n += 88) {
                    var a = c(n);
                    r[a] = (r[a] + t.s) / 2;
                  }
                  return r;
                },
              });
            }),
            s(function (e) {
              var r = e.AnalyserNode.prototype.getFloatFrequencyData;
              n(e.AnalyserNode.prototype, {
                getFloatFrequencyData: function () {
                  for (
                    var e = r.apply(this, arguments), n = 0;
                    n < arguments[0].length;
                    n += 88
                  ) {
                    var o = c(n);
                    arguments[o] = (arguments[o] + t.s) / 2;
                  }
                  return e;
                },
              });
            }),
            s(function (e) {
              return (
                (n = e.HTMLElement.prototype),
                (r = {
                  offsetWidth: function () {
                    return Math.floor(this.getBoundingClientRect().width) + u();
                  },
                  offsetHeight: function () {
                    return (
                      Math.floor(this.getBoundingClientRect().height) + u()
                    );
                  },
                }),
                Object.keys(r).forEach(function (e) {
                  var o = n.__lookupGetter__(e);
                  Object.defineProperty(n, e, {
                    get: function () {
                      return (0 !== t[e] ? r[e] : o).apply(this, arguments);
                    },
                  });
                })
              );
              var n, r;
            }),
            s(function (e) {
              return n(e.Element.prototype, "getClientRects", function () {
                return {
                  0: {
                    x: 0,
                    y: 0,
                    top: 0,
                    bottom: c(500),
                    left: 0,
                    right: c(400),
                    height: c(500),
                    width: c(400),
                    __proto__: e.DOMRect.prototype,
                  },
                  length: 1,
                  __proto__: e.DOMRectList.prototype,
                };
              });
            }),
            s(function (e) {
              return a(e, {
                screen: {
                  availLeft: 0,
                  availTop: 0,
                  availWidth: 1024,
                  availHeight: 768,
                  width: 1024,
                  height: 768,
                  colorDepth: 16,
                  pixelDepth: 16,
                  __proto__: e.Screen.prototype,
                  orientation: {
                    angle: 0,
                    type: "landscape-primary",
                    onchange: null,
                    __proto__: e.ScreenOrientation.prototype,
                  },
                },
              });
            }),
            s(function (e) {
              return n(e.navigator, [
                "getBattery",
                "getGamepads",
                "getVRDisplays",
              ]);
            }),
            s(function (e) {
              return a(e.navigator, {
                platform: "Win32",
                language: "en-US",
                languages: ["en-US"],
              });
            }),
            s(function (e) {
              var t = e.navigator.mediaDevices.enumerateDevices;
              n(e.navigator.mediaDevices, {
                enumerateDevices: function () {
                  return t.apply(this, arguments).then(function (t) {
                    return (
                      t.push({
                        deviceId: "default",
                        groupId: "n/a",
                        kind: "audiooutput",
                        label: "FPS-Audio " + c(400),
                        __proto__: e.MediaDeviceInfo.prototype,
                      }),
                      t
                    );
                  });
                },
              });
            }),
            s(function (e) {
              return n(e, [
                "MediaStreamTrack",
                "RTCPeerConnection",
                "RTCSessionDescription",
                "webkitMediaStreamTrack",
                "webkitRTCPeerConnection",
                "webkitRTCSessionDescription",
              ]);
            }),
            s(function (e) {
              return n(
                e.Intl.DateTimeFormat.prototype,
                "resolvedOptions",
                function () {
                  return {
                    calendar: "gregory",
                    day: "numeric",
                    locale: "en-US",
                    month: "numeric",
                    numberingSystem: "latn",
                    timeZone: "UTC",
                    year: "numeric",
                  };
                }
              );
            }),
            s(function (e) {
              return n(e.Date.prototype, "getTimezoneOffset", function () {
                return [
                  720, 660, 600, 570, 540, 480, 420, 360, 300, 240, 210, 180,
                  120, 60, 0, -60, -120, -180, -210, -240, -270, -300, -330,
                  -345, -360, -390, -420, -480, -510, -525, -540, -570, -600,
                  -630, -660, -720, -765, -780, -840,
                ][c(39)];
              });
            }),
            (e[r] = !0),
            e
          );
        };
        s(window);
        var p =
            window.HTMLIFrameElement.prototype.__lookupGetter__(
              "contentWindow"
            ),
          l =
            window.HTMLIFrameElement.prototype.__lookupGetter__(
              "contentDocument"
            );
        Object.defineProperties(window.HTMLIFrameElement.prototype, {
          contentWindow: {
            get: function () {
              try {
                return s(p.apply(this, arguments));
              } catch (e) {
                return p.apply(this, arguments);
              }
            },
          },
          contentDocument: {
            get: function () {
              try {
                s(p.apply(this, arguments));
              } finally {
                return l.apply(this, arguments);
              }
            },
          },
        }),
          t.nocache && i("ncache", "origin", location.origin);
      }
    };
    if (globalThis.chrome && chrome.storage) {
      var r = chrome.storage.sync,
        o = chrome.runtime.connect({
          name: "fpsbg",
        }),
        a = function (e) {
          var t = e.detail;
          return chrome.storage.local.set(t);
        },
        i = function (e) {
          var t = e.detail;
          return o.postMessage(t);
        },
        c = function (e) {
          return r.get(["v", "s", "g", "w"], function (e) {
            if (null == e.g)
              r.set({
                v: 1039,
                s: ".448398",
                g: Math.random().toString(36).substr(2),
              });
            else {
              if (
                (2048 | e.v) == e.v &&
                ((u = e.w),
                (s = window.location.hostname),
                new RegExp(
                  "^(" + u.replace(/\*/g, ".*").replace(/[\n,]+/g, "|") + ")$"
                ).test(s))
              )
                return;
              window.removeEventListener("clogs", a),
                window.addEventListener("clogs", a),
                window.removeEventListener("ncache", i),
                window.addEventListener("ncache", i);
              var t = JSON.stringify(e),
                o = document.createElement("script");
              (o.text = "(".concat(n, ")('").concat(t, "'")),
                document.documentElement
                  .appendChild(o)
                  .parentNode.removeChild(o);
              var c = document.createElement("script");
              (c.src = chrome.runtime.getURL("skfps.js")),
                (c.onload = function (e) {
                  return window.dispatchEvent(
                    new CustomEvent("evt_skfps", {
                      detail: t,
                    })
                  );
                }),
                document.documentElement
                  .appendChild(c)
                  .parentNode.removeChild(c);
            }
            var u, s;
          });
        };
      c(),
        chrome.storage.onChanged.addListener(function (e) {
          return e.v && c();
        });
    } else {
      window.addEventListener("evt_skfps", function e(t) {
        var r = t.detail;
        n(r), window.removeEventListener("evt_skfps", e);
      });
    }
  },
]);
