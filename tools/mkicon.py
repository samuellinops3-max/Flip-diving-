import zlib, struct
W, H = 320, 176
rows = b""
for y in range(H):
    t = y / H
    if y < int(H * 0.68):
        px = bytes([int(90 + 130 * t), int(190 + 50 * t), 245])
    else:
        px = bytes([20, int(150 - 50 * t), 210])
    rows += b"\x00" + px * W

def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(rows))
       + chunk(b"IEND", b""))
open("ICON0.PNG", "wb").write(png)