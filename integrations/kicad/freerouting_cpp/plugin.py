# plugin.py -- KiCad Action Plugin: route the open board with freerouting-cpp.
#
# Export Specctra DSN -> run freerouting-cpp (in WSL on Windows) -> import Specctra SES.
# Uses the SWIG pcbnew API (pcbnew.ExportSpecctraDSN / ImportSpecctraSES), present in
# KiCad 6 to 10.

import os
import re
import tempfile
import threading
import time

import pcbnew
import wx

from . import runner

PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))


def _safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name) or "board"


def export_dsn(board, path):
    """Writes the board as Specctra DSN; True on success."""
    errors = []
    for args in ((path,), (board, path)):
        try:
            if pcbnew.ExportSpecctraDSN(*args):
                return True, ""
        except Exception as e:  # noqa: BLE001 -- SWIG raises plain exceptions
            errors.append(str(e))
    return False, "; ".join(errors)


def import_ses(board, path):
    """Reads the Specctra session into the board; True on success."""
    errors = []
    for args in ((path,), (board, path)):
        try:
            if pcbnew.ImportSpecctraSES(*args):
                return True, ""
        except Exception as e:  # noqa: BLE001
            errors.append(str(e))
    return False, "; ".join(errors)


def refill_zones(board):
    try:
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    except Exception:  # noqa: BLE001 -- zones are optional, routing is already imported
        pass


class SettingsDialog(wx.Dialog):
    def __init__(self, parent, s):
        wx.Dialog.__init__(self, parent, title="freerouting-cpp: route board",
                           style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER)
        self.s = dict(s)
        grid = wx.FlexGridSizer(cols=2, vgap=6, hgap=10)
        grid.AddGrowableCol(1)

        def spin(label, key, lo, hi, hint):
            grid.Add(wx.StaticText(self, label=label), 0, wx.ALIGN_CENTER_VERTICAL)
            box = wx.BoxSizer(wx.HORIZONTAL)
            ctrl = wx.SpinCtrl(self, min=lo, max=hi, initial=self.s[key], size=(90, -1))
            box.Add(ctrl, 0)
            box.Add(wx.StaticText(self, label=hint), 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 8)
            grid.Add(box, 1, wx.EXPAND)
            return ctrl

        def text(label, key):
            grid.Add(wx.StaticText(self, label=label), 0, wx.ALIGN_CENTER_VERTICAL)
            ctrl = wx.TextCtrl(self, value=self.s[key], size=(380, -1))
            grid.Add(ctrl, 1, wx.EXPAND)
            return ctrl

        self.threads = spin("Threads:", "threads", 1, 64, "optimizer threads (-mt)")
        self.via_costs = spin("Via cost:", "via_costs", 0, 100000, "0 = default; more = fewer vias")
        self.max_passes = spin("Max passes:", "max_passes", 0, 9999, "0 = default")
        self.timeout = spin("Timeout, min:", "timeout_minutes", 0, 24 * 60, "0 = no limit")
        grid.Add((0, 0))
        self.no_optimizer = wx.CheckBox(self, label="Skip the optimizer (faster, longer traces)")
        self.no_optimizer.SetValue(self.s["no_optimizer"])
        grid.Add(self.no_optimizer)
        grid.Add((0, 0))
        self.no_fanout = wx.CheckBox(self, label="Skip the SMD fanout stage (--router.fanout.enabled=false)")
        self.no_fanout.SetValue(self.s["no_fanout"])
        grid.Add(self.no_fanout)
        self.extra = text("Extra arguments:", "extra_args")
        grid.Add((0, 0))
        grid.Add(wx.StaticText(self, label="e.g. --router.automatic_neckdown=false --router.optimizer.max_passes=10"))

        adv = wx.StaticBoxSizer(wx.VERTICAL, self, "Router")
        box = adv.GetStaticBox()
        agrid = wx.FlexGridSizer(cols=2, vgap=6, hgap=10)
        agrid.AddGrowableCol(1)
        agrid.Add(wx.StaticText(box, label="Run:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.mode = wx.RadioBox(box, choices=["Auto", "Native (.exe)", "WSL"], majorDimension=3,
                                style=wx.RA_SPECIFY_COLS)
        self.mode.SetSelection(runner.MODES.index(self.s["mode"]))
        agrid.Add(self.mode)
        agrid.Add(wx.StaticText(box, label="Native executable:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.native = wx.TextCtrl(box, value=self.s["native_path"], size=(380, -1))
        self.native.SetHint("empty = next to the plugin: " + os.path.join("freerouting_cpp", "bin", runner.EXE_NAME))
        agrid.Add(self.native, 1, wx.EXPAND)
        agrid.Add(wx.StaticText(box, label="WSL distribution:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.distro = wx.TextCtrl(box, value=self.s["wsl_distro"], size=(140, -1))
        agrid.Add(self.distro)
        agrid.Add(wx.StaticText(box, label="Router in WSL:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.path = wx.TextCtrl(box, value=self.s["router_path"], size=(380, -1))
        agrid.Add(self.path, 1, wx.EXPAND)
        adv.Add(agrid, 1, wx.EXPAND | wx.ALL, 6)
        self.found = wx.StaticText(box, label="")
        adv.Add(self.found, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM, 6)
        self.mode.Bind(wx.EVT_RADIOBOX, self.show_resolved)
        self.native.Bind(wx.EVT_TEXT, self.show_resolved)

        note = wx.StaticText(self, label="Routing replaces the board's tracks and vias with the router's result.\n"
                                         "Save the board first: the import may not be undoable.")
        top = wx.BoxSizer(wx.VERTICAL)
        top.Add(grid, 0, wx.EXPAND | wx.ALL, 10)
        top.Add(adv, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 10)
        top.Add(note, 0, wx.ALL, 10)
        buttons = self.CreateStdDialogButtonSizer(wx.OK | wx.CANCEL)
        self.FindWindowById(wx.ID_OK).SetLabel("Route")
        top.Add(buttons, 0, wx.EXPAND | wx.ALL, 10)
        self.show_resolved()
        self.SetSizerAndFit(top)
        self.CentreOnParent()

    def show_resolved(self, _evt=None):
        """Says which router the current choice will run."""
        mode, exe = runner.resolve(self.values())
        if mode == "wsl":
            label = "Will run in WSL (%s)." % (self.distro.GetValue().strip() or "default distribution")
        elif exe and os.path.isfile(exe):
            label = "Will run: " + exe
        else:
            label = "Native executable not found: " + (exe or os.path.join(runner.PLUGIN_DIR, "bin", runner.EXE_NAME))
        self.found.SetLabel(label)

    def values(self):
        s = dict(self.s)
        s["threads"] = self.threads.GetValue()
        s["via_costs"] = self.via_costs.GetValue()
        s["max_passes"] = self.max_passes.GetValue()
        s["timeout_minutes"] = self.timeout.GetValue()
        s["no_optimizer"] = self.no_optimizer.GetValue()
        s["extra_args"] = self.extra.GetValue().strip()
        s["no_fanout"] = self.no_fanout.GetValue()
        s["mode"] = runner.MODES[self.mode.GetSelection()]
        s["native_path"] = self.native.GetValue().strip()
        s["wsl_distro"] = self.distro.GetValue().strip()
        s["router_path"] = self.path.GetValue().strip()
        return s


class ProgressDialog(wx.Dialog):
    """Runs the router: status line, progress bar, log, Cancel. Ends with wx.ID_OK when
    the router succeeded, wx.ID_CANCEL otherwise."""

    def __init__(self, parent, run):
        wx.Dialog.__init__(self, parent, title="freerouting-cpp: routing...",
                           style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER)
        self.run = run
        self.state = state = {"alive": True}  # read before touching a possibly deleted dialog
        self.finished = False
        self.started = time.time()

        self.status = wx.StaticText(self, label=run.progress.status)
        self.gauge = wx.Gauge(self, range=1000, size=(560, -1))
        self.elapsed = wx.StaticText(self, label="0:00")
        self.log = wx.TextCtrl(self, style=wx.TE_MULTILINE | wx.TE_READONLY | wx.TE_DONTWRAP, size=(760, 320))
        self.log.SetFont(wx.Font(9, wx.FONTFAMILY_TELETYPE, wx.FONTSTYLE_NORMAL, wx.FONTWEIGHT_NORMAL))
        self.button = wx.Button(self, wx.ID_CANCEL, "Cancel")

        row = wx.BoxSizer(wx.HORIZONTAL)
        row.Add(self.gauge, 1, wx.ALIGN_CENTER_VERTICAL)
        row.Add(self.elapsed, 0, wx.ALIGN_CENTER_VERTICAL | wx.LEFT, 10)
        top = wx.BoxSizer(wx.VERTICAL)
        top.Add(self.status, 0, wx.EXPAND | wx.ALL, 10)
        top.Add(row, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 10)
        top.Add(self.log, 1, wx.EXPAND | wx.ALL, 10)
        top.Add(self.button, 0, wx.ALIGN_RIGHT | wx.RIGHT | wx.BOTTOM, 10)
        self.SetSizerAndFit(top)
        self.CentreOnParent()

        self.button.Bind(wx.EVT_BUTTON, self.on_button)
        self.Bind(wx.EVT_CLOSE, self.on_close)
        self.timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.on_timer, self.timer)

        def later(fn, *args):
            wx.CallAfter(lambda: state["alive"] and fn(*args))

        run.on_line = lambda line: later(self.add_line, line)
        run.on_exit = lambda code: later(self.on_exit, code)
        self.add_line("work directory: " + run.work_dir)
        self.add_line("command: " + " ".join(run.command))
        try:
            run.start()
        except OSError as e:
            self.add_line("cannot start the router: %s" % e)
            later(self.on_exit, None)
        self.timer.Start(200)

    def add_line(self, line):
        self.log.AppendText(runner.short_line(line) + "\n")
        self.status.SetLabel(self.run.progress.status)

    def on_timer(self, _evt):
        secs = int(time.time() - self.started)
        self.elapsed.SetLabel("%d:%02d" % (secs // 60, secs % 60))
        if not self.finished:
            f = self.run.progress.fraction()
            if f is None:
                self.gauge.Pulse()
            else:
                self.gauge.SetValue(int(f * 1000))

    def on_exit(self, code):
        self.finished = True
        self.timer.Stop()
        if self.run.succeeded():
            self.gauge.SetValue(1000)
            self.EndModal(wx.ID_OK)
            return
        if self.run.cancelled:
            self.EndModal(wx.ID_CANCEL)
            return
        self.gauge.SetValue(0)
        msg = "The router failed (exit code %s); the board is unchanged. Log: %s" % (code, self.run.log_path)
        self.add_line(msg)
        self.status.SetLabel(msg)
        self.button.SetLabel("Close")
        self.button.Enable()

    def cancel(self):
        if self.finished:
            self.EndModal(wx.ID_CANCEL)
            return
        self.button.Disable()
        self.status.SetLabel("Cancelling...")
        threading.Thread(target=self.run.cancel, name="freerouting-cpp-cancel").start()

    def on_button(self, _evt):
        self.cancel()

    def on_close(self, _evt):
        self.cancel()

    def Destroy(self):  # noqa: N802 -- wx name
        self.state["alive"] = False
        self.timer.Stop()
        return wx.Dialog.Destroy(self)


class FreeroutingCppPlugin(pcbnew.ActionPlugin):
    def defaults(self):
        self.name = "Route with freerouting-cpp"
        self.category = "PCB auto routing"
        self.description = "Export Specctra DSN, route it with freerouting-cpp, import the SES"
        self.show_toolbar_button = True
        icon = os.path.join(PLUGIN_DIR, "icon.png")
        if os.path.exists(icon):
            self.icon_file_name = icon
            self.dark_icon_file_name = icon

    def Run(self):  # noqa: N802 -- KiCad name
        board = pcbnew.GetBoard()
        parent = wx.FindWindowByName("PcbFrame") or wx.GetActiveWindow()

        s = runner.load_settings()
        dlg = SettingsDialog(parent, s)
        try:
            if dlg.ShowModal() != wx.ID_OK:
                return
            s = dlg.values()
        finally:
            dlg.Destroy()
        runner.save_settings(s)

        name = _safe_name(os.path.splitext(os.path.basename(board.GetFileName() or "board"))[0])
        work = os.path.join(tempfile.gettempdir(), "kicad-freerouting-cpp", name)
        os.makedirs(work, exist_ok=True)
        dsn = os.path.join(work, name + ".dsn")
        ses = os.path.join(work, name + ".ses")
        for f in (dsn, ses):
            if os.path.exists(f):
                os.remove(f)

        ok, err = export_dsn(board, dsn)
        if not ok or not os.path.exists(dsn):
            wx.MessageBox("Specctra DSN export failed.\n%s" % err, "freerouting-cpp", wx.OK | wx.ICON_ERROR, parent)
            return

        run = runner.RouterRun(s, dsn, ses, work)
        pdlg = ProgressDialog(parent, run)
        try:
            result = pdlg.ShowModal()
        finally:
            pdlg.Destroy()
        if result != wx.ID_OK:
            return

        ok, err = import_ses(board, ses)
        if not ok:
            wx.MessageBox("Specctra SES import failed.\n%s\nSession file: %s" % (err, ses),
                          "freerouting-cpp", wx.OK | wx.ICON_ERROR, parent)
            return
        refill_zones(board)
        pcbnew.Refresh()
        p = run.progress
        if p.summary and (p.summary[0] or p.summary[1]):
            wx.MessageBox("Routed with %d unrouted connections and %d clearance violations.\n"
                          "Run the DRC to see them." % (p.summary[0], p.summary[1]),
                          "freerouting-cpp", wx.OK | wx.ICON_WARNING, parent)
