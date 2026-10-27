# Rename the MinGW Qt6Core import so this DLL does not bind to the
# MSVC Qt6Core.dll already loaded in std.exe. The two names are the
# same length, so the PE import string can be swapped in place.
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
old = b"Qt6Core.dll"
new = b"Ft8Core.dll"
count = data.count(old)
if count < 1:
    sys.exit("Qt6Core.dll import was not found in " + str(path))
if len(old) != len(new):
    sys.exit("import name length changed")
path.write_bytes(data.replace(old, new))
print("patched", count)
