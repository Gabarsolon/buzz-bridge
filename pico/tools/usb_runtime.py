"""Find a libusb DLL for PyUSB (default search, else the LIBUSB_DLL environment variable)."""
from pathlib import Path
import usb.core
import usb.backend.libusb1

backend = usb.backend.libusb1.get_backend()
if backend is None:
    import os
    dll = Path(os.environ.get('LIBUSB_DLL', 'libusb-1.0.dll'))
    if dll.is_file():
        backend = usb.backend.libusb1.get_backend(find_library=lambda _:str(dll))
if backend is None:
    raise RuntimeError('libusb-1.0 not found: pip install pyusb libusb-package, or set LIBUSB_DLL')

def find(**kwargs):
    return usb.core.find(backend=backend, **kwargs)
