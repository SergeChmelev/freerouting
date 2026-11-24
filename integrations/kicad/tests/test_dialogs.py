"""Opens the plugin's dialogs in a bare wx app (KiCad's Python, without KiCad itself)
and drives them by timers: settings dialog -> OK, a routing run to the end, a routing run
cancelled after a few seconds. Windows appear briefly on the screen.

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tests/test_dialogs.py <fixtures dir> <work dir> [<freerouting-cpp.exe>]
"""
import os
import shutil
import sys

import wx

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from freerouting_cpp import plugin, runner  # noqa: E402


def main(fixtures, work, exe=None):
    os.makedirs(work, exist_ok=True)
    app = wx.App(False)
    s = runner.load_settings()
    if exe:
        s.update(mode="native", native_path=os.path.abspath(exe))

    dlg = plugin.SettingsDialog(None, s)
    wx.CallLater(800, lambda: dlg.EndModal(wx.ID_OK))
    assert dlg.ShowModal() == wx.ID_OK
    got = dlg.values()
    dlg.Destroy()
    assert got["threads"] == s["threads"] and got["router_path"] == s["router_path"], got
    print("settings dialog ok:", got)

    dlg = plugin.SettingsDialog(None, dict(s, lut=True, lut_via_costs=1500, board_rules=False))
    assert not dlg.via_costs.IsEnabled() and not dlg.no_fanout.IsEnabled() and dlg.lut_via_costs.IsEnabled()
    dlg.lut.SetValue(False)
    dlg.on_lut()
    assert dlg.via_costs.IsEnabled() and not dlg.lut_via_costs.IsEnabled()
    got = dlg.values()
    dlg.Destroy()
    assert (got["lut"], got["lut_via_costs"], got["board_rules"], got["project_files"]) == (False, 1500, False, False), got
    print("LUT checkbox ok")

    small = os.path.join(work, "small.dsn")
    shutil.copy(os.path.join(fixtures, "Issue107-freq_teiler_200kHz_kicad.dsn"), small)
    run = runner.RouterRun(s, small, os.path.join(work, "small.ses"), work, notes=["Taken from the board: test note"])
    dlg = plugin.ProgressDialog(None, run)
    res = dlg.ShowModal()
    text = dlg.log.GetValue()
    dlg.Destroy()
    assert res == wx.ID_OK and run.succeeded(), (res, run.returncode)
    assert text.startswith("Taken from the board: test note"), text[:200]
    print("progress dialog, full run ok:", run.progress.status)

    big = os.path.join(fixtures, "Issue508-DAC2020", "DAC2020_bm01", "DAC2020_bm01.unrouted.dsn")
    run = runner.RouterRun(s, big, os.path.join(work, "big.ses"), work)
    dlg = plugin.ProgressDialog(None, run)
    wx.CallLater(5000, dlg.cancel)
    res = dlg.ShowModal()
    dlg.Destroy()
    assert res == wx.ID_CANCEL and run.cancelled and not run.running(), (res, run.returncode)
    print("progress dialog, cancel ok:", run.progress.status)
    wx.Yield()  # pending CallAfter of the dead dialog must be ignored
    app.Destroy()


if __name__ == "__main__":
    main(*sys.argv[1:4])
