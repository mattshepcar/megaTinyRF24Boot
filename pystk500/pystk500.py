import sys
import serial
from intelhex import IntelHex
import argparse

class Stk500:
    def __init__(self, comport, baudrate, verbose):
        self.ser = serial.Serial(comport, baudrate, parity=serial.PARITY_EVEN, timeout=5, stopbits=serial.STOPBITS_ONE)
        self.verbose = verbose
        
    def connect(self):
        self.ser.write(b'0 ')
        for i in range(5):
            self.ser.write(b'0 ')
            while 1:
                x = self.ser.read()
                if x == b'\x14':
                    x += self.ser.read()
                if not self.ser.in_waiting:
                    break
            if x == b'\x14\x10':
                print('Connected to device on ' + self.ser.port)
                return
            if x == b'\x14\x11':
                raise RuntimeError('Error syncing with device on ' + self.ser.port)
            if not x:
                raise RuntimeError('No response on ' + self.ser.port)
        raise RuntimeError('Unexpected response on ' + self.ser.port)

    def send(self, *args):
        for x in args:
            self.ser.write(x)
        self.ser.write(b' ')
        r = self.ser.read(2)
        if r == b'\x14\x11':
            raise RuntimeError('Failed flashing')
        if r != b'\x14\x10':
            raise RuntimeError('Communication error')
               
    def program(self, start, data):
        segment = start >> 16
        size = len(data)
        if segment == 0:
            type = b'F'
            name = 'program memory'
        elif segment == 0x81:
            type = b'E'
            name = 'EEPROM'
        elif segment == 0x82:
            print('Skipping fuses segment')
            return
        elif segment == 0x85:
            type = b'U'
            name = 'user signatures'
        else:
            print('Unknown segment 0x%08X-0x%08X' % (start, start + len(data)))
            return
    
        for pos in range(0, len(data), 64):
            addr = start + pos
            page = data[pos:pos+64]
            sys.stdout.write('\rWriting %s %i/%i bytes' % (name, pos, size))
            self.send(bytes([0x55, addr&255, (addr>>8)&255]))
            self.send(bytes([0x64, 0, len(page)]), type, page)
        print('\rWriting %s %i/%i bytes' % (name, size, size))
        
    def close(self):
        self.send(b'Q')
        self.ser.close()
        
    def sendcommand(self, cmd):
        self.ser.write(cmd)
        x = self.ser.read()
        while x != b'\n>':
            if not x:
                raise RuntimeError('Communication error')        
            if x and self.verbose:
                sys.stdout.write(x[-1:].decode('utf-8'))
            if x != b'\n':
                x = b''
            x += self.ser.read()
    
def main():
    sys.excepthook = lambda exctype,exc,traceback : print("{}: {}".format(exctype.__name__, exc))
    parser = argparse.ArgumentParser(description="Simple command line"
                                     " interface for UPDI programming")
    parser.add_argument("-c", "--comport", required=True,
                        help="Com port to use (Windows: COMx | *nix: /dev/ttyX)")
    parser.add_argument("-b", "--baudrate", type=int, default=500000)
    parser.add_argument("-f", "--flash", help="Intel HEX file to flash.")
    parser.add_argument("-i", "--id", help="Remote radio ID")
    parser.add_argument("-s", "--setid", help="Reprogram remote radio ID")
    parser.add_argument("-v", "--verbose", action='store_true', help="Verbose output")
    args = parser.parse_args(sys.argv[1:])
    prog = Stk500(args.comport, args.baudrate, args.verbose)   
    if args.id:
        prog.sendcommand(b'*cfg\n')
        prog.sendcommand(b'id %s\n' % args.id.encode('utf-8'))
    if args.setid:
        if not args.id:
            prog.sendcommand(b'*cfg\n')
        prog.sendcommand(b'setid %s\n' % args.setid.encode('utf-8'))
    if args.flash:
        ih = IntelHex()
        ih.loadhex(args.flash)
        prog.connect()
        for start, end in ih.segments():
            prog.program(start, ih.tobinarray(start, end))
        prog.close()
        print("Done!")

if __name__ == "__main__":
    main()
