#include "viewer_theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace ksj::viewer {

void apply_viewer_theme(QApplication& application) {
  // This is intentionally a compact, native-desktop visual language modeled
  // on HDFView's hierarchy + object-info workspace, rather than a dashboard.
  constexpr auto kWindow = "#f0f0f0";
  constexpr auto kSurface = "#ffffff";
  constexpr auto kBorder = "#c9c9c9";
  constexpr auto kSubtleBorder = "#dddddd";
  constexpr auto kText = "#202020";
  constexpr auto kDisabledText = "#8b8b8b";
  constexpr auto kSelection = "#d9e8f5";
  constexpr auto kSelectionText = "#1e2f3d";

  QPalette palette;
  palette.setColor(QPalette::Window, QColor{kWindow});
  palette.setColor(QPalette::WindowText, QColor{kText});
  palette.setColor(QPalette::Base, QColor{kSurface});
  palette.setColor(QPalette::AlternateBase, QColor{"#f8f8f8"});
  palette.setColor(QPalette::Text, QColor{kText});
  palette.setColor(QPalette::PlaceholderText, QColor{kDisabledText});
  palette.setColor(QPalette::Button, QColor{"#efefef"});
  palette.setColor(QPalette::ButtonText, QColor{kText});
  palette.setColor(QPalette::Light, QColor{"#ffffff"});
  palette.setColor(QPalette::Midlight, QColor{kSubtleBorder});
  palette.setColor(QPalette::Mid, QColor{kBorder});
  palette.setColor(QPalette::Dark, QColor{"#7d7d7d"});
  palette.setColor(QPalette::Shadow, QColor{"#616161"});
  palette.setColor(QPalette::Highlight, QColor{kSelection});
  palette.setColor(QPalette::HighlightedText, QColor{kSelectionText});
  palette.setColor(QPalette::Link, QColor{"#165b92"});
  palette.setColor(QPalette::ToolTipBase, QColor{"#ffffe1"});
  palette.setColor(QPalette::ToolTipText, QColor{kText});

  palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor{"#e4eaf0"});
  palette.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor{kText});
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor{kDisabledText});
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor{kDisabledText});
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor{kDisabledText});
  palette.setColor(QPalette::Disabled, QPalette::Button, QColor{"#eeeeee"});
  palette.setColor(QPalette::Disabled, QPalette::Base, QColor{"#f5f5f5"});
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor{"#dddddd"});
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor{kDisabledText});

  application.setStyle(QStringLiteral("Fusion"));
  application.setPalette(palette);
  application.setStyleSheet(QStringLiteral(R"QSS(
    QWidget {
      color: #202020;
      font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
      font-size: 9pt;
    }

    QMainWindow, QDialog, QWidget#viewerRoot {
      background: #f0f0f0;
    }

    QMenuBar {
      background: #f2f2f2;
      border: none;
      border-bottom: 1px solid #c9c9c9;
      padding: 0 2px;
      spacing: 0;
    }

    QMenuBar::item {
      background: transparent;
      padding: 3px 7px;
    }

    QMenuBar::item:selected, QMenuBar::item:pressed {
      background: #e2e2e2;
    }

    QMenu {
      background: #ffffff;
      border: 1px solid #b9b9b9;
      padding: 1px;
    }

    QMenu::item {
      padding: 4px 28px 4px 20px;
    }

    QMenu::item:selected {
      background: #d9e8f5;
      color: #1e2f3d;
    }

    QMenu::separator {
      background: #dddddd;
      height: 1px;
      margin: 2px 3px;
    }

    QToolBar {
      background: #f3f3f3;
      border: none;
      border-bottom: 1px solid #c9c9c9;
      padding: 1px 2px;
      spacing: 1px;
    }

    QToolButton, QPushButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 0;
      color: #202020;
      min-height: 19px;
      padding: 2px 4px;
    }

    QToolButton:hover, QPushButton:hover {
      background: #e3edf6;
      border-color: #a9c3d7;
    }

    QToolButton:pressed, QToolButton:checked, QPushButton:pressed {
      background: #d3e3f0;
      border-color: #84a9c5;
    }

    QToolButton:disabled, QPushButton:disabled {
      background: transparent;
      border-color: transparent;
      color: #8b8b8b;
    }

    /* arrShow-compatible changer: abbreviation / + / value / − / extent. */
    QWidget[arrShowDimension="true"] {
      background: transparent;
      border: 1px solid #a9a9a9;
    }

    QWidget[arrShowDimension="true"][arrShowDimensionActive="true"] {
      background: transparent;
      border-color: #6d9fbe;
    }

    QWidget[arrShowDimension="true"] QToolButton {
      background: transparent;
      border: none;
      border-bottom: 1px solid #c4c4c4;
      border-radius: 0;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 8pt;
      min-height: 14px;
      max-height: 16px;
      padding: 0;
    }

    QWidget[arrShowDimension="true"] QToolButton:hover {
      background: #dbeaf5;
      border-color: #c4c4c4;
    }

    QWidget[arrShowDimension="true"] QToolButton:disabled {
      background: transparent;
      border-bottom: 1px solid #d4d4d4;
      color: #777777;
    }

    QLineEdit[arrShowDimensionValue="true"] {
      background: transparent;
      border: none;
      border-bottom: 1px solid #c4c4c4;
      border-radius: 0;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 9pt;
      min-height: 16px;
      max-height: 18px;
      padding: 0;
    }

    QLineEdit[arrShowDimensionValue="true"][arrShowDimensionFixedIndexInput="true"] {
      background: #ffffff;
    }

    QLineEdit[arrShowDimensionValue="true"]:focus {
      border: 1px solid #6697b8;
      padding: 0;
    }

    QWidget[arrShowDimension="true"][arrShowDimensionActive="true"]
    QLineEdit[arrShowDimensionValue="true"][arrShowDimensionFixedIndexInput="true"] {
      background: #1c5fa8;
      color: #ffffff;
      selection-background-color: #d9e8f5;
      selection-color: #1e2f3d;
    }

    QToolButton[arrShowDimensionLabel="true"] {
      background: transparent;
      border-bottom: none;
      color: #303030;
      font-size: 8pt;
    }

    /* The two arrShow tags select a plane; column order, not colour, defines X/Y. */
    QToolButton[arrShowDimensionLabel="true"][arrShowDimensionSelectionTag="first"],
    QToolButton[arrShowDimensionAbbreviation="true"][arrShowDimensionSelectionTag="first"] {
      color: #1c5fa8;
      font-weight: 700;
    }

    QToolButton[arrShowDimensionLabel="true"][arrShowDimensionSelectionTag="second"],
    QToolButton[arrShowDimensionAbbreviation="true"][arrShowDimensionSelectionTag="second"] {
      color: #b63a31;
      font-weight: 700;
    }

    QToolButton[arrShowDimensionAbbreviation="true"] {
      background: transparent;
      color: #1e4e72;
      font-size: 7pt;
      font-weight: 600;
    }

    QFrame#viewerFileBar {
      background: #f6f6f6;
      border: none;
      border-bottom: 1px solid #c9c9c9;
    }

    QFrame#semanticTreeSurface, QFrame#objectInspectorSurface, QFrame#viewerInfoSurface,
    QWidget[surfaceRole="panel"] {
      background: #ffffff;
      border: none;
    }

    QWidget[surfaceRole="card"] {
      background: transparent;
      border: none;
    }

    QWidget[surfaceRole="controls"] {
      background: #f3f3f3;
      border: 1px solid #d1d1d1;
    }

    QWidget[surfaceRole="canvas"] {
      background: #ffffff;
      border: 1px solid #c9c9c9;
    }

    QLabel[textRole="sectionTitle"] {
      background: #eeeeee;
      border-top: 1px solid #d8d8d8;
      border-bottom: 1px solid #d8d8d8;
      color: #303030;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 10pt;
      font-weight: 400;
      padding: 2px 4px;
    }

    QLabel[textRole="sectionDetail"], QLabel[textRole="inspectorHint"] {
      color: #5f5f5f;
      font-size: 8pt;
      padding: 1px 3px;
    }

    QLabel[textRole="emptyMarker"] {
      color: #567086;
      font-family: "Consolas", monospace;
      font-weight: 600;
      padding: 3px 5px;
    }

    QLabel[textRole="emptyTitle"] {
      color: #303030;
      font-weight: 600;
    }

    QLabel[textRole="emptyDescription"] {
      color: #606060;
      font-size: 8pt;
    }

    QLabel#fileBarReadonlyBadge, QLabel#offlineReadonlyBadge, QLabel#modeBadge, QLabel#workbenchModeBadge {
      background: transparent;
      border: none;
      color: #686868;
      font-size: 8pt;
      font-weight: 400;
      padding: 1px 3px;
    }

    QPlainTextEdit, QTextEdit, QLineEdit {
      background: #ffffff;
      border: 1px solid #c5c5c5;
      border-radius: 0;
      color: #202020;
      padding: 2px 4px;
      selection-background-color: #d9e8f5;
      selection-color: #1e2f3d;
    }

    QPlainTextEdit, QTextEdit {
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      padding: 3px;
    }

    QLabel#metadataXmlSummary {
      background: #f5f8fa;
      border: 1px solid #d7e0e7;
      color: #40505d;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 8pt;
      padding: 3px 5px;
    }

    QTreeWidget#metadataXmlOutline, QPlainTextEdit#metadataXmlPreview {
      background: #fbfcfd;
      border: 1px solid #cbd5dd;
      color: #26333d;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 8pt;
    }

    QTreeWidget#metadataXmlOutline::item {
      min-height: 18px;
    }

    QTreeWidget#metadataXmlOutline::item:selected {
      background: #dbeaf5;
      color: #1e2f3d;
    }

    QSplitter#metadataXmlSplitter::handle {
      background: #d7dfe5;
      width: 1px;
    }

    QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus {
      border: 1px solid #83aeca;
    }

    /* Keep complex selectors and steppers fully native to Fusion. Styling
       only a QComboBox/SpinBox subcontrol suppresses its other affordances. */

    QWidget#objectIdentityInfo QLabel {
      background: #eeeeee;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 10pt;
      padding: 2px 4px;
    }

    QLineEdit#objectNameField, QLineEdit#objectPathField, QLineEdit#objectTypeField, QLineEdit#objectAccessField {
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 10pt;
      min-height: 19px;
      padding: 1px 4px;
    }

    QComboBox QAbstractItemView {
      background: #ffffff;
      border: 1px solid #b9b9b9;
      color: #202020;
      outline: none;
      selection-background-color: #d9e8f5;
      selection-color: #1e2f3d;
    }

    QTabWidget::pane {
      background: #ffffff;
      border: 1px solid #c9c9c9;
      top: -1px;
    }

    QTabBar {
      background: #ededed;
    }

    QTabBar::tab {
      background: #efefef;
      border: 1px solid #d1d1d1;
      border-bottom: none;
      border-radius: 0;
      color: #202020;
      margin-right: 1px;
      min-width: 62px;
      padding: 3px 7px 2px;
    }

    QTabBar::tab:hover {
      background: #f8f8f8;
    }

    QTabBar::tab:selected {
      background: #ffffff;
      border-color: #c9c9c9;
      font-weight: 400;
    }

    QTreeWidget, QTreeView, QTableView, QTableWidget, QListView {
      alternate-background-color: #fbfbfb;
      background: #ffffff;
      border: 1px solid #d4d4d4;
      border-radius: 0;
      color: #202020;
      gridline-color: #e5e5e5;
      outline: none;
      selection-background-color: #d9e8f5;
      selection-color: #1e2f3d;
    }

    QTreeWidget#semanticObjectTree {
      border: none;
      font-size: 9pt;
    }

    QTableWidget#objectSemanticInfoTable, QTableWidget#objectMembersInfoTable,
    QTableWidget#objectAttributesInfo {
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 10pt;
    }

    QTreeWidget::item, QTreeView::item, QTableView::item, QTableWidget::item, QListView::item {
      border: none;
      padding: 1px 4px;
    }

    QTreeWidget::item:hover, QTreeView::item:hover, QTableView::item:hover, QTableWidget::item:hover,
    QListView::item:hover {
      background: #edf4f9;
    }

    QHeaderView::section, QTableCornerButton::section {
      background: #f0f0f0;
      border: none;
      border-right: 1px solid #dddddd;
      border-bottom: 1px solid #c9c9c9;
      color: #303030;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-weight: 400;
      padding: 2px 4px;
    }

    QGroupBox {
      background: #ffffff;
      border: 1px solid #d1d1d1;
      border-radius: 0;
      color: #303030;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 10pt;
      font-weight: 400;
      margin-top: 10px;
      padding: 9px 3px 3px;
    }

    QGroupBox::title {
      background: #eeeeee;
      border-bottom: 1px solid #d8d8d8;
      left: 0;
      padding: 1px 4px;
      subcontrol-origin: margin;
      subcontrol-position: top left;
    }

    QWidget#emptyState, QWidget[emptyState="true"] {
      background: #fafafa;
      border: 1px solid #d1d1d1;
      border-radius: 0;
      color: #606060;
    }

    QLabel#imageCanvas, QLabel#kspaceCanvas {
      background: #151515;
      border: 1px solid #777777;
      border-radius: 0;
      color: #dfe3e6;
    }

    QLabel[textRole="canvasProbe"] {
      background: #111519;
      border: 1px solid #52616c;
      color: #c5d5df;
      font-family: "Consolas", "Microsoft YaHei UI", monospace;
      font-size: 8pt;
      padding: 3px 6px;
    }

    QCheckBox, QRadioButton {
      color: #202020;
      spacing: 4px;
    }

    QCheckBox::indicator, QRadioButton::indicator {
      background: #ffffff;
      border: 1px solid #777777;
      height: 13px;
      width: 13px;
    }

    QCheckBox::indicator:checked, QRadioButton::indicator:checked {
      background: #5f8eaf;
      border-color: #476f8a;
    }

    QSplitter::handle {
      background: #d0d0d0;
      margin: 0;
    }

    QSplitter::handle:hover {
      background: #9abbd2;
    }

    QScrollBar:vertical {
      background: #f0f0f0;
      border: none;
      margin: 0;
      width: 12px;
    }

    QScrollBar::handle:vertical {
      background: #c0c0c0;
      border: 1px solid #a9a9a9;
      min-height: 24px;
    }

    QScrollBar::handle:vertical:hover {
      background: #a8a8a8;
    }

    QScrollBar:horizontal {
      background: #f0f0f0;
      border: none;
      height: 12px;
      margin: 0;
    }

    QScrollBar::handle:horizontal {
      background: #c0c0c0;
      border: 1px solid #a9a9a9;
      min-width: 24px;
    }

    QScrollBar::handle:horizontal:hover {
      background: #a8a8a8;
    }

    QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
      background: transparent;
      border: none;
    }

    QStatusBar {
      background: #f0f0f0;
      border-top: 1px solid #c9c9c9;
      color: #4f4f4f;
      padding: 1px 4px;
    }

    QToolTip {
      background: #ffffe1;
      border: 1px solid #777777;
      color: #202020;
      padding: 2px;
    }
  )QSS"));
}

} // namespace ksj::viewer
