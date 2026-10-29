# moc output includes a header that pulls in complex.h. That header
# defines I, and moc's generated templates use I as a parameter name.
# Insert the undefs immediately after that include.
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
needle = sys.argv[2] if len(sys.argv) > 2 else "decoderq65.h"
text = path.read_text(encoding="utf-8")
insert = "#undef I\n#undef complex\n"
found = None
for line in text.splitlines(keepends=True):
    stripped = line.lstrip()
    if stripped.startswith("#include") and needle in stripped:
        found = line
        break
if found is None:
    sys.stderr.write("moc output has no #include of %s\n" % needle)
    sys.exit(1)
if insert not in text.split(found, 1)[1][:80]:
    text = text.replace(found, found + insert, 1)
    path.write_text(text, encoding="utf-8", newline="\n")
