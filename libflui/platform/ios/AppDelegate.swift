import Flutter
import UIKit
import QuickLook
import PhotosUI
import UniformTypeIdentifiers

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    if let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "FluiFiles") {
      FluiFiles.register(with: registrar)
    }
  }
}

// SDK-owned local file access; the caller retains ownership of the source.
final class FluiFiles: NSObject, FlutterPlugin, UIDocumentPickerDelegate,
    QLPreviewControllerDataSource, QLPreviewControllerDelegate, UIAdaptivePresentationControllerDelegate, PHPickerViewControllerDelegate {
  private weak var registrar: FlutterPluginRegistrar?
  private var pending: FlutterResult?
  private var mediaMultiple = false
  private var directory: URL?
  private var item: URL?
  private var presented: UIViewController?

  static func register(with registrar: FlutterPluginRegistrar) {
    let plugin = FluiFiles()
    plugin.registrar = registrar
    let channel = FlutterMethodChannel(name: "devkit.libflui/files", binaryMessenger: registrar.messenger())
    registrar.addMethodCallDelegate(plugin, channel: channel)
  }

  func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    guard call.method == "view" || call.method == "export" || call.method == "pick_media" else {
      result(FlutterMethodNotImplemented); return
    }
    guard pending == nil else { result(["status": 6, "path": ""]); return }
    if call.method == "pick_media" {
      pending = result
      mediaMultiple = (call.arguments as? [String: Any])?["multiple"] as? Bool ?? false
      showMedia(); return
    }
    guard let args = call.arguments as? [String: String],
          let source = args["source"], source.hasPrefix("/"), !source.contains("\0"),
          let name = args["name"], !name.isEmpty, name != ".", name != "..",
          !name.contains("/"), !name.contains("\\"), !name.contains("\0") else {
      result(["status": 1, "path": ""]); return
    }
    pending = result
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("libflui-file-" + UUID().uuidString)
    let copy = folder.appendingPathComponent(name)
    directory = folder
    // Copy off the UI thread. Neither preview editing nor export can alter SDK data.
    DispatchQueue.global(qos: .userInitiated).async {
      do {
        let original = URL(fileURLWithPath: source)
        let type = try original.resourceValues(forKeys: [.isRegularFileKey, .isDirectoryKey])
        guard type.isRegularFile == true || (call.method == "export" && type.isDirectory == true) else {
          throw CocoaError(.fileReadUnsupportedScheme)
        }
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: false)
        try FileManager.default.copyItem(at: original, to: copy)
        DispatchQueue.main.async { self.show(copy, export: call.method == "export") }
      } catch {
        DispatchQueue.main.async { self.finish(7) }
      }
    }
  }

  private func showMedia() {
    guard let owner = registrar?.viewController, owner.viewIfLoaded?.window != nil,
          owner.presentedViewController == nil, !owner.isBeingDismissed else {
      finish(6); return
    }
    var config = PHPickerConfiguration()
    config.filter = .any(of: [.images, .videos])
    config.selectionLimit = mediaMultiple ? 9 : 1
    if mediaMultiple { config.selection = .ordered }
    config.preferredAssetRepresentationMode = .current
    let picker = PHPickerViewController(configuration: config)
    picker.delegate = self
    presented = picker
    owner.present(picker, animated: true)
    picker.presentationController?.delegate = self
  }

  func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
    guard picker === presented else { return }
    picker.dismiss(animated: true) {
      // The selection is fixed before copying; no partially copied batch escapes.
      self.presented = nil
      guard !results.isEmpty else { self.finish(0); return }
      guard results.count <= (self.mediaMultiple ? 9 : 1) else { self.finish(1); return }
      let folder = FileManager.default.temporaryDirectory.appendingPathComponent("libflui-media-" + UUID().uuidString)
      self.copyMedia(results, index: 0, folder: folder, paths: [])
    }
  }

  private func copyMedia(_ results: [PHPickerResult], index: Int, folder: URL, paths: [String]) {
    guard pending != nil else { return }
    if index == results.count {
      if mediaMultiple {
        guard let data = try? JSONSerialization.data(withJSONObject: paths),
              let value = String(data: data, encoding: .utf8) else {
          try? FileManager.default.removeItem(at: folder); finish(7); return
        }
        finish(0, path: value)
      } else { finish(0, path: paths[0]) }
      return
    }
    let provider = results[index].itemProvider
    guard let type = provider.registeredTypeIdentifiers.first(where: {
      guard let type = UTType($0) else { return false }
      return type.conforms(to: .image) || type.conforms(to: .movie)
    }) else { try? FileManager.default.removeItem(at: folder); finish(10); return }
    provider.loadFileRepresentation(forTypeIdentifier: type) { url, error in
      do {
        guard let url, error == nil else { throw CocoaError(.fileReadUnknown) }
        // Provider URLs expire with this callback. Copy each item sequentially,
        // retain original bytes/names and isolate equal names in separate folders.
        let destination = folder.appendingPathComponent(String(index))
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let copy = destination.appendingPathComponent(url.lastPathComponent)
        try FileManager.default.copyItem(at: url, to: copy)
        DispatchQueue.main.async { self.copyMedia(results, index: index + 1, folder: folder, paths: paths + [copy.path]) }
      } catch {
        try? FileManager.default.removeItem(at: folder)
        DispatchQueue.main.async { self.finish(7) }
      }
    }
  }

  private func show(_ copy: URL, export: Bool) {
    guard let owner = registrar?.viewController, owner.viewIfLoaded?.window != nil,
          owner.presentedViewController == nil, !owner.isBeingDismissed else {
      finish(6); return
    }
    item = copy
    let controller: UIViewController
    if export {
      let picker = UIDocumentPickerViewController(forExporting: [copy], asCopy: true)
      picker.delegate = self
      controller = picker
    } else {
      guard QLPreviewController.canPreview(copy as NSURL) else { finish(10); return }
      let preview = QLPreviewController()
      preview.dataSource = self
      preview.delegate = self
      controller = preview
    }
    presented = controller
    controller.modalPresentationStyle = .fullScreen
    owner.present(controller, animated: true)
    controller.presentationController?.delegate = self
  }

  private func finish(_ status: Int, path: String = "") {
    guard let result = pending else { return }
    let folder = directory
    pending = nil; directory = nil; item = nil; presented = nil; mediaMultiple = false
    result(["status": status, "path": path])
    if let folder { try? FileManager.default.removeItem(at: folder) }
  }

  func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
    guard controller === presented else { return }
    if urls.count == 1 { finish(0, path: urls[0].absoluteString) } else { finish(7) }
  }
  func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
    if controller === presented { finish(0) }
  }
  func presentationControllerDidDismiss(_ controller: UIPresentationController) {
    if controller.presentedViewController === presented {
      finish(0, path: presented is QLPreviewController ? item?.absoluteString ?? "" : "")
    }
  }
  func numberOfPreviewItems(in controller: QLPreviewController) -> Int { item == nil ? 0 : 1 }
  func previewController(_ controller: QLPreviewController, previewItemAt index: Int) -> QLPreviewItem {
    item! as NSURL
  }
  func previewController(_ controller: QLPreviewController, editingModeFor previewItem: QLPreviewItem) -> QLPreviewItemEditingMode {
    .disabled
  }
  func previewControllerDidDismiss(_ controller: QLPreviewController) {
    if controller === presented { finish(0, path: item?.absoluteString ?? "") }
  }
}
