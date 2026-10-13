import AppKit
import SwiftUI

/// Semantic AppKit surfaces keep analyst controls readable in both appearances.
enum WorkspaceDesign {
  static let canvas = Color(nsColor: .windowBackgroundColor)
  static let surface = Color(nsColor: .controlBackgroundColor)
  static let border = Color(nsColor: .separatorColor).opacity(0.55)
}

struct AnalystGroupBoxStyle: GroupBoxStyle {
  func makeBody(configuration: Configuration) -> some View {
    VStack(alignment: .leading, spacing: 12) {
      configuration.label.font(.system(size: 13, weight: .semibold))
      configuration.content.frame(maxWidth: .infinity, alignment: .leading)
    }
    .padding(16)
    .background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 10))
    .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(WorkspaceDesign.border))
  }
}

struct WorkspaceSectionHeading: View {
  var title: String
  var count: Int
  var body: some View {
    HStack {
      Text(title).font(.system(size: 13, weight: .semibold))
      Text(count.formatted()).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
        .padding(.horizontal, 7).padding(.vertical, 2)
        .background(.quaternary, in: Capsule())
      Spacer()
    }
  }
}

struct MissionSummaryCard: View {
  var title: String
  var count: Int
  var icon: String
  var action: () -> Void
  var body: some View {
    Button(action: action) {
      VStack(alignment: .leading, spacing: 12) {
        HStack {
          Image(systemName: icon).foregroundStyle(.tint)
          Spacer()
          Image(systemName: "arrow.up.right").foregroundStyle(.tertiary)
        }
        Text(count.formatted()).font(.system(size: 28, weight: .medium, design: .rounded))
          .monospacedDigit()
        Text(title).font(.callout).foregroundStyle(.secondary)
      }.padding(16).frame(maxWidth: .infinity, alignment: .leading)
        .background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(WorkspaceDesign.border))
        .contentShape(RoundedRectangle(cornerRadius: 10))
    }.buttonStyle(.plain).help("Open \(title.lowercased())")
  }
}
