from intelhex import IntelHex
import argparse, struct, random

def crc16(bytes, crc=0xFFFF):
    for byte in bytes:
        crc ^= byte << 8
        for i in range(8):
            crc <<= 1
            if crc & 0x10000:
                crc ^= 0x11021
    return crc

def crc16reverse(bytes, crc=0):
    for byte in reversed(bytes):
        for i in range(8):
            if crc & 1:
                crc ^= 0x11021
            crc >>= 1
        crc ^= byte << 8
    return crc

def fixcrc16pos(buffer, fixpos, desiredcrc=0xFFFF):
    crc = struct.pack('>H', crc16(buffer[:fixpos]))
    return crc16reverse(crc + buffer[fixpos+2:], desiredcrc)

def main():
    parser = argparse.ArgumentParser(description="Bootloader CRC patcher")
    parser.add_argument('filename', help='Intel HEX file to patch')
    args = parser.parse_args()

    if not args.filename.lower().endswith('.hex'):
        print("Not a .hex file?")
        return

    ih = IntelHex()
    ih.loadhex(args.filename)
    data = ih.tobinstr(start=0, size=0x100)
    offset = data.find(b'\xCC\xCC')
    if offset < 0:
        return
    crc = fixcrc16pos(data, offset)
    print('Patching with CRC of %04X' % crc)
    ih[offset] = crc >> 8
    ih[offset + 1] = crc & 255
    #data = ih.tobinstr(start=0, size=0x100)
    #print('Final CRC = %04X' % crc16(data))
    ih.write_hex_file(args.filename)

if __name__ == "__main__":
    main()
