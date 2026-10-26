"""Linux virtual gamepad backend, built on the kernel's uinput via python-evdev.

No driver install needed (uinput is built into the kernel) -- only permission
to open /dev/uinput. See README for the one-time udev rule so this doesn't
need to run as root.

Registers as a standard gamepad (BTN_GAMEPAD-range buttons + a resting analog
stick axis) so SDL's joystick/gamecontroller subsystem enumerates it the same
way it would a real pad, which is what lets PCSX2's BuzzDevice bind to it
through its normal controller-binding UI.
"""
from evdev import UInput, AbsInfo, ecodes as e

_BUTTON_MAP = {
    'red':    e.BTN_TR,      # right shoulder, matches Windows XUSB_GAMEPAD_RIGHT_SHOULDER
    'blue':   e.BTN_NORTH,
    'orange': e.BTN_WEST,
    'green':  e.BTN_SOUTH,
    'yellow': e.BTN_EAST,
}
BUTTONS = tuple(_BUTTON_MAP)

_CAPABILITIES = {
    e.EV_KEY: list(_BUTTON_MAP.values()) + [e.BTN_START, e.BTN_SELECT,
                                             e.BTN_THUMBL, e.BTN_THUMBR,
                                             e.BTN_TL, e.BTN_TR2, e.BTN_TL2],
    # A resting stick axis: several SDL joystick backends only classify a
    # /dev/input/event* node as a full joystick (rather than a plain
    # keyboard-like device) once at least one ABS axis is present.
    e.EV_ABS: [
        (e.ABS_X, AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)),
        (e.ABS_Y, AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)),
    ],
}


class VirtualPad:
    """One virtual controller = one Buzz buzzer slot."""

    def __init__(self, name='Buzz Web Controller'):
        self._ui = UInput(_CAPABILITIES, name=name, vendor=0x045e,
                          product=0x028e, version=0x110)

    def press(self, button: str):
        self._ui.write(e.EV_KEY, _BUTTON_MAP[button], 1)
        self._ui.syn()

    def release(self, button: str):
        self._ui.write(e.EV_KEY, _BUTTON_MAP[button], 0)
        self._ui.syn()

    def release_all(self):
        for code in _BUTTON_MAP.values():
            self._ui.write(e.EV_KEY, code, 0)
        self._ui.syn()

    def close(self):
        self.release_all()
        self._ui.close()


def available() -> bool:
    try:
        import evdev  # noqa: F401
        import os
        return os.path.exists('/dev/uinput')
    except Exception:
        return False
