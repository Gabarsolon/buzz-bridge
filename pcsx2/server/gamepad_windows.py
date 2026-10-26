"""Windows virtual gamepad backend, built on ViGEmBus via the `vgamepad` package.

Creates a virtual Xbox 360 controller. SDL (and therefore PCSX2) enumerates it
exactly like a real pad, which is what lets PCSX2's BuzzDevice bind to it
through its normal "press any button" controller-binding UI -- no PCSX2 code
is touched.

Button layout matches a real Buzz! buzzer's five buttons, using the same
XInput face-button convention PCSX2's existing BuzzDevice1 config already
uses (RightShoulder/Y/X/A/B) -- see README for why.
"""
import vgamepad as vg

_BUTTON_MAP = {
    'red':    vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER,
    'blue':   vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
    'orange': vg.XUSB_BUTTON.XUSB_GAMEPAD_X,
    'green':  vg.XUSB_BUTTON.XUSB_GAMEPAD_A,
    'yellow': vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
}
BUTTONS = tuple(_BUTTON_MAP)


class VirtualPad:
    """One virtual controller = one Buzz buzzer slot."""

    def __init__(self):
        self._pad = vg.VX360Gamepad()

    def press(self, button: str):
        self._pad.press_button(_BUTTON_MAP[button])
        self._pad.update()

    def release(self, button: str):
        self._pad.release_button(_BUTTON_MAP[button])
        self._pad.update()

    def release_all(self):
        for b in _BUTTON_MAP.values():
            self._pad.release_button(b)
        self._pad.update()

    def close(self):
        self.release_all()


def available() -> bool:
    try:
        import vgamepad  # noqa: F401
        return True
    except Exception:
        return False
