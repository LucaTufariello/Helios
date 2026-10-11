import AppKit
import GodEyeCore
import SwiftUI

struct RawDataView: View {
  @ObservedObject var model: WorkspaceModel
  @State private var text = ""
  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Picker("Data", selection: $model.rawScope) {
        ForEach(
          ["Selected record", "Optical image", "SAR acquisition", "Change review", "Mission"],
          id: \.self
        ) {
          Text($0).tag($0)
        }
      }.padding(.horizontal, 14).padding(.top, 12)
      Text("Read-only metadata snapshot · Refresh to capture current data").font(.caption)
        .foregroundStyle(.secondary).padding(.horizontal, 14)
      ScrollView([.vertical, .horizontal]) {
        Text(text).font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
          .frame(maxWidth: .infinity, alignment: .leading).padding(14)
      }.background(Color(nsColor: .textBackgroundColor))
      HStack {
        Button("Refresh") { refresh() }
        Button("Copy JSON") {
          NSPasteboard.general.clearContents()
          NSPasteboard.general.setString(text, forType: .string)
        }
        Spacer()
      }.padding(.horizontal, 14).padding(.bottom, 12)
    }.onAppear { refresh() }.onChange(of: model.rawScope) { _, _ in refresh() }
  }
  private func refresh() {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    encoder.dateEncodingStrategy = .iso8601
    let data: Data?
    switch model.rawScope {
    case "Mission": data = model.missions.plan.flatMap { try? encoder.encode($0) }
    case "SAR acquisition": data = model.sar.currentJob.flatMap { try? encoder.encode($0.result) }
    case "Optical image": data = model.optical.image.flatMap { try? encoder.encode($0.result) }
    case "Change review": data = model.satellite.review.flatMap { try? encoder.encode($0.result) }
    default: data = model.selected.flatMap { try? encoder.encode(MissionRecord($0)) }
    }
    guard let data else {
      text = "No item selected for this data source."
      return
    }
    guard data.count <= 1024 * 1024 else {
      text = "Metadata exceeds the display limit. Use the source export."
      return
    }
    text = String(decoding: data, as: UTF8.self)
  }
}
