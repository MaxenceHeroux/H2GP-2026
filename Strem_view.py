import csv
import html
import json
import math
import re
import random
import sys
import time
from collections import deque
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

from PyQt5 import QtCore, QtGui, QtWidgets
import pyqtgraph as pg


APP_DIR = Path(__file__).resolve().parent
ALIASES_FILE = APP_DIR / "port_aliases.json"
EXPORT_DIR = APP_DIR / "exports"
SERIAL_BAUDRATE = 115200
RETENTION_SECONDS = 30 * 60


METRIC_ORDER = ["temp_1", "temp_2", "tension", "pression", "temp_tube"]
METRIC_GRID = [["temp_1", "temp_2"], ["tension", "pression", "temp_tube"]]
METRIC_LABELS = {
    "temp_1": "Temp_1",
    "temp_2": "Temp_2",
    "tension": "Tension",
    "pression": "Pression",
    "temp_tube": "Temp_tube",
}
METRIC_UNITS = {
    "temp_1": "°C",
    "temp_2": "°C",
    "tension": "mV",
    "pression": "kPa",
    "temp_tube": "°C",
}
GRAPH_WINDOWS = {
    "10 s": 10,
    "30 s": 30,
    "1 min": 60,
    "2 min": 120,
}


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def format_duration(seconds: float) -> str:
    seconds = max(0, int(seconds))
    if seconds < 60:
        return f"{seconds}s"
    minutes, secs = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes}m {secs:02d}s"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h {minutes:02d}m"


def safe_float(value: str):
    try:
        return float(value.replace(",", "."))
    except (TypeError, ValueError):
        return None


def detect_ports():
    ports = []
    for port in list_ports.comports():
        ports.append(port)
    return ports


def auto_detect_port(ports):
    for port in ports:
        if port.vid == 0x0483:
            return port.device
    return ports[0].device if ports else None


def load_aliases():
    if not ALIASES_FILE.exists():
        return {}
    try:
        return json.loads(ALIASES_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def save_aliases(aliases):
    try:
        ALIASES_FILE.write_text(json.dumps(aliases, indent=2, ensure_ascii=False), encoding="utf-8")
    except OSError:
        pass


def parse_serial_line(line: str):
    raw = line.strip()
    if not raw:
        return None

    log_match = re.match(r"^\[(?P<stamp>\d+)\]\[(?P<level>[A-Z]+)\]\s*(?P<message>.*)$", raw)
    if log_match:
        stamp = int(log_match.group("stamp"))
        level = log_match.group("level")
        message = log_match.group("message").strip()
    else:
        stamp = None
        level = "INFO"
        message = raw

    numeric_patterns = {
        "temp_1": r"Temp_1\s+(-?\d+(?:[\.,]\d+)?)",
        "temp_2": r"Temp_2\s+(-?\d+(?:[\.,]\d+)?)",
        "temp_tube": r"Temp_tube\s*=\s*(-?\d+(?:[\.,]\d+)?)",
        "tension": r"Tension\s+(-?\d+(?:[\.,]\d+)?)",
        "pression": r"Pression\s*=\s*(-?\d+(?:[\.,]\d+)?)",
    }

    values = {}
    for metric, pattern in numeric_patterns.items():
        match = re.search(pattern, message, flags=re.IGNORECASE)
        if match:
            parsed = safe_float(match.group(1))
            if parsed is not None:
                values[metric] = parsed

    state_match = re.search(r"\b(EV|CC)\s+(ON|OFF)\b", message, flags=re.IGNORECASE)
    state = None
    if state_match:
        state = {
            "name": state_match.group(1).upper(),
            "value": state_match.group(2).upper(),
        }

    if not values and not state:
        return {
            "timestamp": stamp,
            "level": level,
            "message": message,
            "values": {},
            "state": None,
            "raw": raw,
        }

    return {
        "timestamp": stamp,
        "level": level,
        "message": message,
        "values": values,
        "state": state,
        "raw": raw,
    }


class MetricPanel(QtWidgets.QFrame):
    def __init__(self, metric_key: str, title: str, units: str, color: str):
        super().__init__()
        self.metric_key = metric_key
        self.units = units
        self.color = color
        self.setObjectName("MetricCard")
        self.setFrameShape(QtWidgets.QFrame.StyledPanel)
        self.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Preferred)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)

        header = QtWidgets.QHBoxLayout()
        self.title_label = QtWidgets.QLabel(title)
        self.title_label.setObjectName("MetricTitle")
        self.window_label = QtWidgets.QLabel("Fenêtre: 10 s")
        self.window_label.setObjectName("MetricWindow")
        header.addWidget(self.title_label)
        header.addStretch(1)
        header.addWidget(self.window_label)

        controls = QtWidgets.QHBoxLayout()
        self.avg_window_spin = QtWidgets.QSpinBox()
        self.avg_window_spin.setRange(1, 3600)
        self.avg_window_spin.setValue(10)
        self.avg_window_spin.setSuffix(" s")

        self.graph_window_combo = QtWidgets.QComboBox()
        self.graph_window_combo.addItems(list(GRAPH_WINDOWS.keys()) + ["Custom"])
        self.graph_window_combo.setCurrentText("30 s")

        self.graph_window_spin = QtWidgets.QSpinBox()
        self.graph_window_spin.setRange(5, RETENTION_SECONDS)
        self.graph_window_spin.setValue(30)
        self.graph_window_spin.setSuffix(" s")
        self.graph_window_spin.setVisible(False)

        controls.addWidget(QtWidgets.QLabel("Moy custom"))
        controls.addWidget(self.avg_window_spin)
        controls.addStretch(1)
        controls.addWidget(QtWidgets.QLabel("Graphe"))
        controls.addWidget(self.graph_window_combo)
        controls.addWidget(self.graph_window_spin)

        self.latest_label = QtWidgets.QLabel("--")
        self.latest_label.setObjectName("MetricLatest")
        self.aux_label = QtWidgets.QLabel(f"Moy 5s: --   |   Moy custom: --   |   {units}")
        self.aux_label.setWordWrap(True)

        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground(None)
        self.plot_widget.showGrid(x=True, y=True, alpha=0.25)
        self.plot_widget.setMenuEnabled(False)
        self.plot_widget.setMouseEnabled(x=False, y=False)
        self.plot_widget.addLegend(offset=(10, 10))
        self.plot_widget.setLabel("bottom", "Temps", units="s")
        self.plot_widget.setLabel("left", "Valeur")
        self.plot_widget.setMinimumHeight(210)
        self.curve = self.plot_widget.plot(pen=pg.mkPen(color, width=2), name=title)

        layout.addLayout(header)
        layout.addLayout(controls)
        layout.addWidget(self.latest_label)
        layout.addWidget(self.aux_label)
        layout.addWidget(self.plot_widget)

    def set_values(self, latest_text: str, avg_5s_text: str, avg_custom_text: str):
        self.latest_label.setText(latest_text)
        self.aux_label.setText(f"Moy 5s: {avg_5s_text}   |   Moy custom: {avg_custom_text}")

    def graph_window_seconds(self):
        if self.graph_window_combo.currentText() == "Custom":
            return self.graph_window_spin.value()
        return GRAPH_WINDOWS[self.graph_window_combo.currentText()]

    def custom_avg_seconds(self):
        return self.avg_window_spin.value()

    def refresh_window_label(self):
        self.window_label.setText(f"Fenêtre: {self.graph_window_seconds()} s")


class DashboardWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("H2Trace Dashboard")
        self.resize(1650, 960)

        self.aliases = load_aliases()
        self.ports = []
        self.serial = None
        self.is_paused = True
        self.dev_mode = False
        self.theme_mode = "dark"
        self.current_port_key = None
        self.auto_started = False
        self.current_time_window = GRAPH_WINDOWS["30 s"]
        self.custom_average_window = 10
        self.samples = {metric: deque() for metric in METRIC_ORDER}
        self.numeric_history = []
        self.raw_log_history = deque()
        self.state_map = {
            "EV": {"value": "--", "since": None, "changed_at": None},
            "CC": {"value": "--", "since": None, "changed_at": None},
        }
        self.fake_state = {
            "temp_1": 26.8,
            "temp_2": 26.6,
            "tension": 2508.0,
            "pression": -0.44,
            "temp_tube": 27.95,
            "EV": "OFF",
            "CC": "OFF",
            "last_state_flip": time.monotonic(),
            "last_sample_emit": 0.0,
        }

        self._build_ui()
        self._apply_theme()
        self.refresh_ports()

        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.poll_serial)
        self.timer.start(50)

    def _build_ui(self):
        central = QtWidgets.QWidget()
        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(10, 10, 10, 10)
        root.setSpacing(10)

        root.addWidget(self._build_top_bar())

        splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal)
        splitter.setChildrenCollapsible(False)
        splitter.addWidget(self._build_log_panel())
        splitter.addWidget(self._build_dashboard_panel())
        splitter.setStretchFactor(0, 4)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([1280, 320])
        root.addWidget(splitter, 1)

        self.setCentralWidget(central)

    def _build_top_bar(self):
        bar = QtWidgets.QFrame()
        bar.setObjectName("TopBar")
        layout = QtWidgets.QHBoxLayout(bar)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(10)

        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.currentIndexChanged.connect(self._on_port_changed)
        self.port_combo.setMinimumWidth(430)

        self.alias_edit = QtWidgets.QLineEdit()
        self.alias_edit.setPlaceholderText("Alias pour le port sélectionné")
        self.alias_edit.editingFinished.connect(self._save_current_alias)

        self.refresh_button = QtWidgets.QPushButton("Rafraîchir")
        self.refresh_button.clicked.connect(self.refresh_ports)

        self.play_button = QtWidgets.QPushButton("Play")
        self.play_button.clicked.connect(self.resume_stream)

        self.pause_button = QtWidgets.QPushButton("Pause")
        self.pause_button.clicked.connect(self.pause_stream)

        self.clear_button = QtWidgets.QPushButton("Clear")
        self.clear_button.clicked.connect(self.clear_data)

        self.save_button = QtWidgets.QPushButton("Sauvegarde 30 min")
        self.save_button.clicked.connect(self.export_last_30_minutes)

        self.theme_combo = QtWidgets.QComboBox()
        self.theme_combo.addItems(["Sombre", "Clair"])
        self.theme_combo.currentTextChanged.connect(self._on_theme_changed)

        self.dev_mode_toggle = QtWidgets.QCheckBox("Dev mode")
        self.dev_mode_toggle.stateChanged.connect(self._on_dev_mode_changed)

        layout.addWidget(QtWidgets.QLabel("COM"))
        layout.addWidget(self.port_combo, 3)
        layout.addWidget(self.alias_edit, 2)
        layout.addWidget(self.refresh_button)
        layout.addSpacing(12)
        layout.addWidget(self.play_button)
        layout.addWidget(self.pause_button)
        layout.addWidget(self.clear_button)
        layout.addWidget(self.save_button)
        layout.addSpacing(12)
        layout.addWidget(QtWidgets.QLabel("Thème"))
        layout.addWidget(self.theme_combo)
        layout.addSpacing(8)
        layout.addWidget(self.dev_mode_toggle)

        return bar

    def _build_log_panel(self):
        panel = QtWidgets.QFrame()
        panel.setObjectName("SidePanel")
        layout = QtWidgets.QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        title = QtWidgets.QLabel("Sortie série brute")
        title.setObjectName("SectionTitle")

        self.log_view = QtWidgets.QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setAcceptRichText(True)
        self.log_view.document().setMaximumBlockCount(600)

        state_box = QtWidgets.QFrame()
        state_box.setObjectName("ChartFrame")
        state_layout = QtWidgets.QVBoxLayout(state_box)
        state_layout.setContentsMargins(10, 10, 10, 10)
        state_layout.setSpacing(6)

        state_title = QtWidgets.QLabel("États EV / CC")
        state_title.setObjectName("SectionTitle")
        state_layout.addWidget(state_title)

        self.state_table = QtWidgets.QTableWidget(2, 3)
        self.state_table.setHorizontalHeaderLabels(["État actuel", "Depuis", "Dernière mise à jour"])
        self.state_table.setVerticalHeaderLabels(["EV", "CC"])
        self.state_table.horizontalHeader().setStretchLastSection(True)
        self.state_table.verticalHeader().setDefaultSectionSize(34)
        self.state_table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.state_table.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
        self.state_table.setFocusPolicy(QtCore.Qt.NoFocus)
        state_layout.addWidget(self.state_table)

        layout.addWidget(title)
        layout.addWidget(self.log_view, 1)
        layout.addWidget(state_box)
        return panel

    def _build_dashboard_panel(self):
        panel = QtWidgets.QFrame()
        panel.setObjectName("SidePanel")
        layout = QtWidgets.QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        header = QtWidgets.QHBoxLayout()
        header.addWidget(QtWidgets.QLabel("Indicateurs"))
        header.addStretch(1)
        layout.addLayout(header)

        self.metric_panels = {}
        metrics_grid = QtWidgets.QGridLayout()
        metrics_grid.setSpacing(10)

        positions = {
            "temp_1": (0, 0),
            "temp_2": (0, 1),
            "tension": (1, 0),
            "pression": (1, 1),
            "temp_tube": (1, 2),
        }

        for metric in METRIC_ORDER:
            panel_widget = MetricPanel(metric, METRIC_LABELS[metric], METRIC_UNITS[metric], self._metric_color(metric))
            panel_widget.avg_window_spin.valueChanged.connect(lambda value, key=metric: self._on_metric_avg_window_changed(key, value))
            panel_widget.graph_window_combo.currentTextChanged.connect(lambda text, key=metric: self._on_metric_graph_window_changed(key, text))
            panel_widget.graph_window_spin.valueChanged.connect(lambda value, key=metric: self._on_metric_graph_custom_changed(key, value))
            panel_widget.graph_window_combo.currentTextChanged.connect(panel_widget.refresh_window_label)
            panel_widget.graph_window_spin.valueChanged.connect(panel_widget.refresh_window_label)
            panel_widget.refresh_window_label()
            self.metric_panels[metric] = panel_widget

            row, col = positions[metric]
            metrics_grid.addWidget(panel_widget, row, col)

        layout.addLayout(metrics_grid)

        layout.addStretch(1)
        return panel

    def _apply_theme(self):
        dark = self.theme_mode == "dark"
        colors = {
            "window_bg": "#10151f" if dark else "#f5f7fb",
            "panel_bg": "#161d2a" if dark else "#ffffff",
            "panel_border": "#263244" if dark else "#cfd8e3",
            "text": "#e8eef7" if dark else "#122033",
            "muted_text": "#9fb8d3" if dark else "#52657b",
            "title_text": "#ffffff" if dark else "#0f172a",
            "button_bg": "#243146" if dark else "#e8eef7",
            "button_hover": "#30405a" if dark else "#d7e0ea",
            "button_pressed": "#1d2736" if dark else "#c7d3e0",
            "control_bg": "#0f1520" if dark else "#ffffff",
            "control_border": "#2a3647" if dark else "#c1ccd8",
            "header_bg": "#1d2838" if dark else "#eef3f8",
            "header_text": "#dce7f3" if dark else "#122033",
            "selection": "#3d6fb6" if dark else "#6b9df2",
            "tooltip_bg": "#0b1220" if dark else "#ffffff",
            "tooltip_text": "#e8eef7" if dark else "#122033",
            "log_bg": "#0b1220" if dark else "#ffffff",
            "graph_bg": None,
            "graph_grid": "#6b7280" if dark else "#cbd5e1",
        }

        app = QtWidgets.QApplication.instance()
        if app is not None:
            palette = QtGui.QPalette()
            palette.setColor(QtGui.QPalette.Window, QtGui.QColor(colors["window_bg"]))
            palette.setColor(QtGui.QPalette.WindowText, QtGui.QColor(colors["text"]))
            palette.setColor(QtGui.QPalette.Base, QtGui.QColor(colors["control_bg"]))
            palette.setColor(QtGui.QPalette.AlternateBase, QtGui.QColor(colors["panel_bg"]))
            palette.setColor(QtGui.QPalette.Text, QtGui.QColor(colors["text"]))
            palette.setColor(QtGui.QPalette.Button, QtGui.QColor(colors["button_bg"]))
            palette.setColor(QtGui.QPalette.ButtonText, QtGui.QColor(colors["text"]))
            palette.setColor(QtGui.QPalette.Highlight, QtGui.QColor(colors["selection"]))
            palette.setColor(QtGui.QPalette.HighlightedText, QtGui.QColor("#ffffff" if dark else "#ffffff"))
            palette.setColor(QtGui.QPalette.ToolTipBase, QtGui.QColor(colors["tooltip_bg"]))
            palette.setColor(QtGui.QPalette.ToolTipText, QtGui.QColor(colors["tooltip_text"]))
            app.setPalette(palette)

        self.setStyleSheet(
            f"""
            QWidget {{
                font-family: Segoe UI;
                font-size: 12px;
                color: {colors['text']};
            }}
            QMainWindow {{
                background: {colors['window_bg']};
            }}
            QFrame#TopBar, QFrame#SidePanel, QFrame#ChartFrame, QFrame#MetricCard {{
                background: {colors['panel_bg']};
                border: 1px solid {colors['panel_border']};
                border-radius: 12px;
            }}
            QLabel#SectionTitle {{
                font-size: 15px;
                font-weight: 700;
                color: {colors['title_text']};
            }}
            QLabel#MetricTitle {{
                font-size: 13px;
                font-weight: 700;
                color: {colors['muted_text']};
            }}
            QLabel#MetricWindow {{
                color: {colors['muted_text']};
            }}
            QLabel#MetricLatest {{
                font-size: 22px;
                font-weight: 700;
                color: {colors['title_text']};
            }}
            QPushButton {{
                background: {colors['button_bg']};
                border: 1px solid {colors['control_border']};
                border-radius: 8px;
                padding: 7px 12px;
            }}
            QPushButton:hover {{
                background: {colors['button_hover']};
            }}
            QPushButton:pressed {{
                background: {colors['button_pressed']};
            }}
            QLineEdit, QComboBox, QSpinBox, QTextEdit, QTableWidget {{
                background: {colors['control_bg']};
                border: 1px solid {colors['control_border']};
                border-radius: 8px;
                selection-background-color: {colors['selection']};
                selection-color: #ffffff;
            }}
            QComboBox::drop-down {{
                border: 0px;
                width: 24px;
            }}
            QComboBox::down-arrow {{
                width: 10px;
                height: 10px;
            }}
            QComboBox QAbstractItemView, QTableView, QListView {{
                background: {colors['panel_bg']};
                color: {colors['text']};
                border: 1px solid {colors['control_border']};
                selection-background-color: {colors['selection']};
                selection-color: #ffffff;
                outline: 0;
            }}
            QTableWidget::item, QTableView::item, QListView::item {{
                padding: 6px;
            }}
            QHeaderView::section {{
                background: {colors['header_bg']};
                color: {colors['header_text']};
                border: none;
                padding: 6px;
            }}
            QTextEdit {{
                font-family: Consolas;
                background: {colors['log_bg']};
            }}
            QToolTip {{
                background-color: {colors['tooltip_bg']};
                color: {colors['tooltip_text']};
                border: 1px solid {colors['panel_border']};
            }}
            """
        )

        for metric, panel in getattr(self, "metric_panels", {}).items():
            panel.plot_widget.setBackground(colors["window_bg"])
            panel.plot_widget.getPlotItem().getAxis("left").setPen(pg.mkPen(colors["text"]))
            panel.plot_widget.getPlotItem().getAxis("bottom").setPen(pg.mkPen(colors["text"]))
            panel.plot_widget.getPlotItem().getAxis("left").setTextPen(pg.mkPen(colors["text"]))
            panel.plot_widget.getPlotItem().getAxis("bottom").setTextPen(pg.mkPen(colors["text"]))
            panel.plot_widget.getPlotItem().showGrid(x=True, y=True, alpha=0.25)

        if hasattr(self, "log_view"):
            self.log_view.setStyleSheet(f"background: {colors['log_bg']};")

    def _on_theme_changed(self, text: str):
        self.theme_mode = "dark" if text == "Sombre" else "light"
        self._apply_theme()

    def refresh_ports(self):
        previous_selection = self.port_combo.currentData()
        self.ports = detect_ports()
        auto_port = auto_detect_port(self.ports)
        self.port_combo.blockSignals(True)
        self.port_combo.clear()

        for port in self.ports:
            alias = self.aliases.get(port.device, "")
            label = f"{alias} | {port.device} | {port.description}" if alias else f"{port.device} | {port.description}"
            self.port_combo.addItem(label, port.device)

        self.port_combo.blockSignals(False)

        if self.port_combo.count() == 0:
            self.port_combo.addItem("Aucun port détecté", None)
            self.alias_edit.setText("")
            self._close_serial()
            return

        target_index = 0
        if previous_selection is not None:
            for index in range(self.port_combo.count()):
                if self.port_combo.itemData(index) == previous_selection:
                    target_index = index
                    break
        elif self.current_port_key:
            for index in range(self.port_combo.count()):
                if self.port_combo.itemData(index) == self.current_port_key:
                    target_index = index
                    break
        else:
            if auto_port:
                for index in range(self.port_combo.count()):
                    if self.port_combo.itemData(index) == auto_port:
                        target_index = index
                        break

        self.port_combo.setCurrentIndex(target_index)
        auto_selected = self.port_combo.itemData(target_index) == auto_port and auto_port is not None
        self._on_port_changed(target_index, open_serial=not auto_selected)
        if auto_selected and not self.dev_mode and not self.auto_started and self.serial is None:
            self.resume_stream()
            self.auto_started = True

    def _on_port_changed(self, index: int, open_serial: bool = True):
        if index < 0:
            return
        port = self.port_combo.itemData(index)
        if not port:
            return

        self.current_port_key = port
        self.alias_edit.blockSignals(True)
        self.alias_edit.setText(self.aliases.get(port, ""))
        self.alias_edit.blockSignals(False)
        if open_serial:
            self._open_serial(port)

    def _save_current_alias(self):
        if not self.current_port_key:
            return
        alias = self.alias_edit.text().strip()
        if alias:
            self.aliases[self.current_port_key] = alias
        else:
            self.aliases.pop(self.current_port_key, None)
        save_aliases(self.aliases)
        self.refresh_ports()

    def _open_serial(self, port_name: str):
        self._close_serial()
        try:
            self.serial = serial.Serial(port_name, SERIAL_BAUDRATE, timeout=0)
            self.append_log("INFO", f"Connexion ouverte sur {port_name}")
        except Exception as exc:
            self.serial = None
            self.append_log("ERROR", f"Impossible d'ouvrir {port_name}: {exc}")

    def _close_serial(self):
        if self.serial is not None:
            try:
                self.serial.close()
            except Exception:
                pass
        self.serial = None

    def pause_stream(self):
        self.is_paused = True
        self.append_log("INFO", "Acquisition en pause")

    def resume_stream(self):
        if self.serial is None:
            if self.current_port_key is None:
                if not self.dev_mode:
                    self.append_log("ERROR", "Aucun port COM sélectionné")
                    return
            else:
                self._open_serial(self.current_port_key)
                if self.serial is None and not self.dev_mode:
                    return
        self.is_paused = False
        self.auto_started = True
        self.append_log("INFO", "Acquisition reprise")

    def clear_data(self):
        for metric in METRIC_ORDER:
            self.samples[metric].clear()
        self.numeric_history.clear()
        self.raw_log_history.clear()
        for state in self.state_map.values():
            state["value"] = "--"
            state["since"] = None
        self.log_view.clear()
        self.append_log("INFO", "Données effacées")

    def export_last_30_minutes(self):
        ensure_dir(EXPORT_DIR)
        filename = EXPORT_DIR / f"h2trace_export_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        cutoff = time.monotonic() - RETENTION_SECONDS
        rows = [row for row in self.numeric_history if row["mono_ts"] >= cutoff]

        try:
            with filename.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow([
                    "timestamp",
                    "level",
                    "message",
                    "temp_1_c",
                    "temp_2_c",
                    "tension_mv",
                    "pression_kpa",
                    "pression_bar",
                    "temp_tube_c",
                    "ev_state",
                    "cc_state",
                ])
                for row in rows:
                    writer.writerow([
                        row["timestamp_text"],
                        row["level"],
                        row["message"],
                        row.get("temp_1", ""),
                        row.get("temp_2", ""),
                        row.get("tension", ""),
                        row.get("pression", ""),
                        f"{row['pression'] / 100:.6f}" if row.get("pression") is not None else "",
                        row.get("temp_tube", ""),
                        row.get("ev", ""),
                        row.get("cc", ""),
                    ])
            self.append_log("INFO", f"Export CSV créé: {filename.name}")
        except OSError as exc:
            self.append_log("ERROR", f"Export impossible: {exc}")

    def _metric_color(self, metric: str) -> str:
        return {
            "temp_1": "#ff4d4f",
            "temp_2": "#52c41a",
            "tension": "#fadb14",
            "pression": "#13c2c2",
            "temp_tube": "#9254de",
        }[metric]

    def _on_metric_avg_window_changed(self, metric: str, value: int):
        if metric in self.metric_panels:
            self.metric_panels[metric].avg_window_spin.setValue(value)

    def _on_metric_graph_window_changed(self, metric: str, text: str):
        panel = self.metric_panels.get(metric)
        if panel is not None:
            panel.graph_window_spin.setVisible(text == "Custom")
            panel.refresh_window_label()

    def _on_metric_graph_custom_changed(self, metric: str, value: int):
        panel = self.metric_panels.get(metric)
        if panel is not None:
            panel.refresh_window_label()

    def _on_dev_mode_changed(self, state: int):
        self.dev_mode = state == QtCore.Qt.Checked
        if self.dev_mode:
            self.is_paused = False
            self._close_serial()
            self.append_log("WARN", "Mode dev activé: données simulées en cours")
        else:
            self.append_log("INFO", "Mode dev désactivé")

    def _fake_tick(self):
        now = time.monotonic()
        if now - self.fake_state["last_sample_emit"] < 0.35:
            return
        self.fake_state["last_sample_emit"] = now

        self.fake_state["temp_1"] += random.uniform(-0.05, 0.05)
        self.fake_state["temp_2"] += random.uniform(-0.05, 0.05)
        self.fake_state["tension"] += random.uniform(-8, 8)
        self.fake_state["pression"] += random.uniform(-0.015, 0.015)
        self.fake_state["temp_tube"] += random.uniform(-0.04, 0.04)

        if now - self.fake_state["last_state_flip"] > random.uniform(5, 15):
            self.fake_state["last_state_flip"] = now
            self.fake_state["EV"] = "ON" if self.fake_state["EV"] == "OFF" else "OFF"
            if random.random() < 0.4:
                self.fake_state["CC"] = "ON" if self.fake_state["CC"] == "OFF" else "OFF"

        temp_1 = round(self.fake_state["temp_1"], 2)
        temp_2 = round(self.fake_state["temp_2"], 2)
        tension = round(self.fake_state["tension"], 0)
        pression = round(self.fake_state["pression"], 3)
        temp_tube = round(self.fake_state["temp_tube"], 2)

        timestamp_text = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        message = f"Temp_1 {temp_1} C | Temp_2 {temp_2} C | Tension {int(tension)} mV | Pression = {pression} kPa | Temp_tube = {temp_tube} C"
        self.append_log("INFO", message)

        values = {
            "temp_1": temp_1,
            "temp_2": temp_2,
            "tension": tension,
            "pression": pression,
            "temp_tube": temp_tube,
        }

        for state_name in ["EV", "CC"]:
            state_value = self.fake_state[state_name]
            state = self.state_map[state_name]
            if state["value"] != state_value:
                state["value"] = state_value
                state["since"] = now
                state["changed_at"] = datetime.now()
                self.append_log("INFO", f"{state_name} -> {state_value}")

        self._append_sample(values, "INFO", message, timestamp_text, None, None)

        self._prune_old_samples()
        self._update_cards()
        self._update_state_table()
        self._update_graph()

    def append_log(self, level: str, message: str):
        color_map = {
            "INFO": "#70d58a",
            "WARN": "#f2c14e",
            "WARNING": "#f2c14e",
            "ERROR": "#ff6b6b",
            "DEBUG": "#95a5a6",
        }
        color = color_map.get(level.upper(), "#dce7f3")
        timestamp = datetime.now().strftime("%H:%M:%S")
        html_message = html.escape(message)
        html_line = f'<span style="color:#8ea3bf">[{timestamp}]</span> <span style="color:{color}; font-weight:600">[{level.upper()}]</span> <span style="color:#e8eef7">{html_message}</span>'
        self.log_view.append(html_line)
        self.log_view.moveCursor(QtGui.QTextCursor.End)

    def _prune_old_samples(self):
        cutoff = time.monotonic() - RETENTION_SECONDS
        for metric in METRIC_ORDER:
            samples = self.samples[metric]
            while samples and samples[0][0] < cutoff:
                samples.popleft()

        while self.raw_log_history and self.raw_log_history[0]["mono_ts"] < cutoff:
            self.raw_log_history.popleft()

        self.numeric_history = [row for row in self.numeric_history if row["mono_ts"] >= cutoff]

    def _average_over_window(self, metric: str, window_seconds: int):
        if window_seconds <= 0:
            return None
        cutoff = time.monotonic() - window_seconds
        values = [value for ts, value in self.samples[metric] if ts >= cutoff]
        if not values:
            return None
        return sum(values) / len(values)

    def _latest_value(self, metric: str):
        samples = self.samples[metric]
        if not samples:
            return None
        return samples[-1][1]

    def _update_cards(self):
        for metric in METRIC_ORDER:
            panel = self.metric_panels[metric]
            latest = self._latest_value(metric)
            avg_5s = self._average_over_window(metric, 5)
            avg_custom = self._average_over_window(metric, panel.custom_avg_seconds())

            if latest is None:
                latest_text = "--"
            elif metric == "pression":
                latest_text = f"{latest:.3f} kPa / {latest / 100:.4f} bar"
            elif metric == "tension":
                latest_text = f"{latest:.0f} mV"
            else:
                latest_text = f"{latest:.2f} {METRIC_UNITS[metric]}"

            if avg_5s is None:
                avg_5s_text = "--"
            elif metric == "pression":
                avg_5s_text = f"{avg_5s:.3f} kPa / {avg_5s / 100:.4f} bar"
            elif metric == "tension":
                avg_5s_text = f"{avg_5s:.0f} mV"
            else:
                avg_5s_text = f"{avg_5s:.2f} {METRIC_UNITS[metric]}"

            if avg_custom is None:
                avg_custom_text = "--"
            elif metric == "pression":
                avg_custom_text = f"{avg_custom:.3f} kPa / {avg_custom / 100:.4f} bar"
            elif metric == "tension":
                avg_custom_text = f"{avg_custom:.0f} mV"
            else:
                avg_custom_text = f"{avg_custom:.2f} {METRIC_UNITS[metric]}"

            panel.set_values(latest_text, avg_5s_text, avg_custom_text)

    def _update_state_table(self):
        now = time.monotonic()
        for row_index, name in enumerate(["EV", "CC"]):
            state = self.state_map[name]
            current = state["value"]
            since = state["since"]
            duration = format_duration(now - since) if since is not None else "--"
            last_change = state["changed_at"].strftime("%H:%M:%S") if state["changed_at"] else "--"

            values = [current, duration, last_change]
            for col_index, value in enumerate(values):
                item = self.state_table.item(row_index, col_index)
                if item is None:
                    item = QtWidgets.QTableWidgetItem()
                    self.state_table.setItem(row_index, col_index, item)
                item.setText(value)
                if col_index == 0:
                    item.setForeground(QtGui.QBrush(QtGui.QColor("#70d58a" if current == "ON" else "#ff6b6b" if current == "OFF" else "#dce7f3")))

    def _append_sample(self, metric_values: dict, log_level: str, message: str, timestamp_text: str, state_name=None, state_value=None):
        mono_ts = time.monotonic()
        row = {
            "mono_ts": mono_ts,
            "timestamp_text": timestamp_text,
            "level": log_level,
            "message": message,
        }

        for metric, value in metric_values.items():
            self.samples[metric].append((mono_ts, value))
            row[metric] = value

        if "pression" in metric_values:
            row["pression_bar"] = metric_values["pression"] / 100.0

        if state_name == "EV":
            row["ev"] = state_value
        elif state_name == "CC":
            row["cc"] = state_value

        if self.state_map["EV"]["value"] != "--":
            row.setdefault("ev", self.state_map["EV"]["value"])
        if self.state_map["CC"]["value"] != "--":
            row.setdefault("cc", self.state_map["CC"]["value"])

        self.numeric_history.append(row)
        self.raw_log_history.append(row)

    def poll_serial(self):
        if self.is_paused:
            self._update_cards()
            self._update_state_table()
            self._update_graph()
            return

        if self.dev_mode:
            self._fake_tick()
            return

        if self.serial is None:
            if self.current_port_key is not None:
                self._open_serial(self.current_port_key)
            if self.serial is None:
                self._update_cards()
                self._update_state_table()
                self._update_graph()
                return

        try:
            while self.serial.in_waiting:
                line = self.serial.readline().decode(errors="ignore")
                parsed = parse_serial_line(line)
                if not parsed:
                    continue

                timestamp_text = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                self.append_log(parsed["level"], parsed["message"])

                if parsed["state"]:
                    state_name = parsed["state"]["name"]
                    new_value = parsed["state"]["value"]
                    current_state = self.state_map[state_name]
                    if current_state["value"] != new_value:
                        current_state["value"] = new_value
                        current_state["since"] = time.monotonic()
                        current_state["changed_at"] = datetime.now()
                        self.append_log("INFO", f"{state_name} -> {new_value}")

                if parsed["values"] or parsed["state"]:
                    self._append_sample(
                        parsed["values"],
                        parsed["level"],
                        parsed["message"],
                        timestamp_text,
                        parsed["state"]["name"] if parsed["state"] else None,
                        parsed["state"]["value"] if parsed["state"] else None,
                    )

                self._prune_old_samples()

        except Exception as exc:
            self.append_log("ERROR", f"Erreur lecture série: {exc}")
            self._close_serial()

        self._update_cards()
        self._update_state_table()
        self._update_graph()

    def _update_graph(self):
        now = time.monotonic()

        for metric in METRIC_ORDER:
            panel = self.metric_panels[metric]
            window_seconds = panel.graph_window_seconds()
            start = now - window_seconds
            samples = [(ts, value) for ts, value in self.samples[metric] if ts >= start]

            if not samples:
                panel.curve.setData([], [])
                continue

            x = [ts - now for ts, _ in samples]
            y = [value for _, value in samples]
            panel.curve.setData(x, y)
            panel.plot_widget.setXRange(-window_seconds, 0, padding=0.01)

            min_y = min(y)
            max_y = max(y)
            if math.isclose(min_y, max_y):
                padding = abs(max_y) * 0.1 or 1.0
            else:
                padding = max((max_y - min_y) * 0.12, 0.5)
            panel.plot_widget.setYRange(min_y - padding, max_y + padding, padding=0)


def main():
    pg.setConfigOptions(antialias=True)
    app = QtWidgets.QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()