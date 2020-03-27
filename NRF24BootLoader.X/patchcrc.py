from intelhex import IntelHex
import argparse

def crc16(bytes, crc=0xFFFF):
    for byte in bytes:
        crc ^= byte << 8
        for i in range(8):
            crc <<= 1
            if crc & 0x10000:
                crc ^= 0x11021
    return crc

def main():
    parser = argparse.ArgumentParser(description="Bootloader CRC patcher")
    parser.add_argument('filename', help='Intel HEX file to patch')
    args = parser.parse_args()

    ih = IntelHex()
    ih.loadhex(args.filename)
    data = ih.tobinarray(start=0, size=0xFE)
    crc = crc16(data) ^ 0x84CF
    print('Patching with CRC of %04X' % crc)
    ih[0xFE] = crc >> 8
    ih[0xFF] = crc & 255
    ih.write_hex_file(args.filename)

if __name__ == "__main__":
    main()
