"""Scoped Jianghe panel colors for the AI assistant and its settings dialog.

These are the neutral dark colors used by the engineering layer tree and the
recognition panels. Applying this theme never changes the application palette.
"""

from qgis.PyQt.QtCore import Qt
from qgis.PyQt.QtGui import QColor, QPalette


DOCK_STYLE = """
QDockWidget#JiangheLibreGeoLensDock {
    color:#d4d4d4; background:#1e1e1e; border:1px solid #3c3c3c;
    margin:0; padding:0;
}
QDockWidget#JiangheLibreGeoLensDock::title {
    color:#f1f5f9; background:#181818; border:0;
    padding:8px 38px 8px 10px; font-weight:600;
}
QDockWidget#JiangheLibreGeoLensDock::close-button {
    image:url(:/images/themes/default/mIconCloseTab.svg);
    background:transparent; border:0; width:30px; height:26px;
}
QDockWidget#JiangheLibreGeoLensDock::close-button:hover {
    image:url(:/images/themes/default/mIconCloseTabHover.svg);
    background:#c42b1c;
}
QDockWidget#JiangheLibreGeoLensDock::float-button { background:transparent; border:0; }
QDockWidget#JiangheLibreGeoLensDock::float-button:hover { background:#3a3d41; }
"""


PANEL_STYLE = """
QWidget { color:#d4d4d4; font-size:12px; }
QWidget#jiangheAiLensRoot, QDialog#jiangheAiLensSettings {
    background:#1e1e1e; color:#d4d4d4;
}
QLabel { background:transparent; border:0; color:#d4d4d4; }
QLabel#sectionTitle, QLabel#messageRole { color:#f1f5f9; font-weight:600; }
QLabel#messageMeta, QLabel#modelSummary, QLabel#executionStatus { color:#9d9d9d; }
QLabel#imageStatus { color:#9d9d9d; }
QLabel#imagePreview {
    background:#252526; border:1px solid #3c3c3c; border-radius:4px;
    color:#9d9d9d;
}
QFrame#aiHeader { background:#1e1e1e; border:0; border-bottom:1px solid #3c3c3c; }
QFrame#aiComposer {
    background:#252526; border:1px solid #454545; border-radius:6px;
}
QFrame#aiComposer[active="true"] { border-color:#007fd4; }
QFrame#userMessage { background:#252526; border:1px solid #3c3c3c; border-radius:6px; }
QFrame#assistantMessage { background:#1e1e1e; border:0; }
QFrame#executionCard { background:#252526; border:1px solid #3c3c3c; border-radius:4px; }
QLabel#executionStatus[status="active"] { color:#75beff; }
QLabel#executionStatus[status="complete"] { color:#89d185; }
QLabel#executionStatus[status="error"] { color:#f48771; }
QLineEdit, QComboBox, QPlainTextEdit {
    color:#d4d4d4; background:#252526; border:1px solid #454545;
    border-radius:3px; padding:5px 7px; selection-color:#ffffff;
    selection-background-color:#094771;
}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color:#007fd4; }
QLineEdit:disabled, QComboBox:disabled { color:#777777; border-color:#343434; }
QComboBox { min-height:24px; padding-right:28px; background:#2d2d30; }
QComboBox::drop-down {
    subcontrol-origin:padding; subcontrol-position:top right; width:24px;
    border-left:1px solid #454545; background:#333337;
}
QComboBox::down-arrow {
    image:url(:/images/themes/default/mActionArrowDown.svg); width:12px; height:12px;
}
QComboBox QAbstractItemView {
    color:#d4d4d4; background:#252526; border:1px solid #454545;
    selection-color:#ffffff; selection-background-color:#0e639c; outline:0;
}
QComboBox QAbstractItemView::item { min-height:26px; padding:4px 8px; }
QTextBrowser {
    color:#d4d4d4; background:transparent; border:0; padding:0;
    selection-color:#ffffff; selection-background-color:#094771;
}
QScrollArea#aiConversation, QScrollArea#aiConversation > QWidget > QWidget {
    background:#1e1e1e; border:0;
}
QPlainTextEdit#aiPrompt { background:#252526; border:0; padding:6px 8px; }
QPushButton {
    color:#d4d4d4; background:#2d2d30; border:1px solid #454545;
    border-radius:3px; padding:3px 10px; min-height:24px;
}
QPushButton:hover { color:#ffffff; background:#3a3d41; border-color:#5a5a5a; }
QPushButton:pressed { background:#45484d; }
QPushButton#primaryButton, QPushButton[primary="true"] {
    color:#ffffff; background:#0e639c; border-color:#1177bb;
}
QPushButton#primaryButton:hover, QPushButton[primary="true"]:hover {
    background:#1177bb; border-color:#3794ff;
}
QPushButton#primaryButton:pressed, QPushButton[primary="true"]:pressed { background:#094771; }
QPushButton:disabled {
    color:#777777; background:#252526; border-color:#343434;
}
QToolButton {
    color:#d4d4d4; background:transparent; border:0; border-radius:3px;
    padding:5px;
}
QToolButton:hover { color:#ffffff; background:#3a3d41; }
QToolButton:pressed { background:#45484d; }
QToolButton:disabled { color:#777777; }
QCheckBox { color:#d4d4d4; spacing:6px; background:transparent; }
QProgressBar#aiProgress {
    background:#2d2d30; border:0; border-radius:2px; min-height:3px; max-height:3px;
}
QProgressBar#aiProgress::chunk { background:#0e639c; border:0; border-radius:2px; }
QScrollBar:vertical {
    background:transparent; width:9px; margin:0;
}
QScrollBar::handle:vertical { background:#424242; min-height:28px; border-radius:4px; }
QScrollBar::handle:vertical:hover { background:#686868; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
QScrollBar:horizontal { background:transparent; height:9px; margin:0; }
QScrollBar::handle:horizontal { background:#424242; min-width:28px; border-radius:4px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }
QToolTip { color:#d4d4d4; background:#252526; border:1px solid #454545; padding:4px; }
"""


def _apply_window_palette(widget):
    palette = widget.palette()
    for group in (QPalette.ColorGroup.Active, QPalette.ColorGroup.Inactive,
                  QPalette.ColorGroup.Disabled):
        palette.setColor(group, QPalette.ColorRole.Window, QColor("#1e1e1e"))
        palette.setColor(group, QPalette.ColorRole.WindowText, QColor("#d4d4d4"))
        palette.setColor(group, QPalette.ColorRole.Text, QColor("#d4d4d4"))
        palette.setColor(group, QPalette.ColorRole.Base, QColor("#252526"))
        palette.setColor(group, QPalette.ColorRole.Highlight, QColor("#0e639c"))
        palette.setColor(group, QPalette.ColorRole.HighlightedText, QColor("#ffffff"))
    widget.setPalette(palette)
    widget.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
    widget.setAutoFillBackground(True)


def apply_assistant_theme(dock, root):
    """Apply the app's neutral panel colors to this dock and its content only."""
    _apply_window_palette(dock)
    _apply_window_palette(root)
    dock.setStyleSheet(DOCK_STYLE)
    root.setStyleSheet(PANEL_STYLE)


def apply_settings_theme(dialog):
    """Keep floating model settings consistent without styling other dialogs."""
    dialog.setObjectName("jiangheAiLensSettings")
    _apply_window_palette(dialog)
    dialog.setStyleSheet(PANEL_STYLE)
