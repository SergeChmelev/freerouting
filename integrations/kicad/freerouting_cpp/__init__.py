# KiCad Action Plugin "Route with freerouting-cpp" (see ../README.md).
# Inside KiCad (pcbnew and wx importable) the plugin registers itself; outside KiCad
# only runner.py is usable, e.g. for tests.
try:
    import pcbnew  # noqa: F401
    import wx  # noqa: F401
except ImportError:
    pcbnew = None

# register() needs the KiCad application (it asserts in a bare Python with pcbnew).
if pcbnew is not None and wx.GetApp() is not None:
    from .plugin import FreeroutingCppPlugin

    FreeroutingCppPlugin().register()
