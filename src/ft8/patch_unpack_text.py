# Copy MSHV's unpack_msg.cpp into the object folder with one Qt 6 fix.
# unpacktext() writes msg[0] .. msg[22] into a string that starts empty.
# Qt 5's QString::operator[] grew the string for that; Qt 6's does not,
# so a JT65 free-text message (or a VHF/UHF shorthand try) writes past the
# end and crashes. Padding msg to 23 spaces first is what Qt 5 did.
# The MSHV source file itself is not changed.
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
text = src.read_text(encoding="latin-1")
head = "QString PackUnpackMsg::unpacktext(int nc1,int nc2,int nc3,QString msg)\n{\n"
if text.count(head) != 1:
    sys.exit("unpacktext() was not found once in " + str(src))
fix = "    if (msg.size() < 23) msg = msg.leftJustified(23, QLatin1Char(' ')); // STD: Qt 6 operator[] does not grow\n"
out.write_text(text.replace(head, head + fix), encoding="latin-1")
print("patched", out)
