#include "CommandLine.hpp"
#include <algorithm>
#include "Platform.h"

struct PartInfo
{
	uint8_t signature[3];
	uint16_t flashSize;
	uint8_t pageSize;
	const char* name;
};

const PartInfo parts [] =
{
	{{0x1E, 0x91, 0x23}, 0x800, 0x40, "ATtiny202"},
	{{0x1E, 0x91, 0x21}, 0x800, 0x40, "ATtiny212"},
	{{0x1E, 0x91, 0x22}, 0x800, 0x40, "ATtiny204"},
	{{0x1E, 0x91, 0x20}, 0x800, 0x40, "ATtiny214"},

	{{0x1E, 0x92, 0x27}, 0x1000, 0x40, "ATtiny402"},
	{{0x1E, 0x92, 0x23}, 0x1000, 0x40, "ATtiny412"},
	{{0x1E, 0x92, 0x26}, 0x1000, 0x40, "ATtiny404"},
	{{0x1E, 0x92, 0x22}, 0x1000, 0x40, "ATtiny414"},
	{{0x1E, 0x92, 0x25}, 0x1000, 0x40, "ATtiny406"},
	{{0x1E, 0x92, 0x21}, 0x1000, 0x40, "ATtiny416"},
	{{0x1E, 0x92, 0x20}, 0x1000, 0x40, "ATtiny417"},

	{{0x1E, 0x93, 0x25}, 0x2000, 0x40, "ATtiny804"},
	{{0x1E, 0x93, 0x22}, 0x2000, 0x40, "ATtiny814"},
	{{0x1E, 0x93, 0x24}, 0x2000, 0x40, "ATtiny806"},
	{{0x1E, 0x93, 0x21}, 0x2000, 0x40, "ATtiny816"},
	{{0x1E, 0x93, 0x23}, 0x2000, 0x40, "ATtiny807"},
	{{0x1E, 0x93, 0x20}, 0x2000, 0x40, "ATtiny817"},

	{{0x1E, 0x94, 0x25}, 0x4000, 0x40, "ATtiny1604"},
	{{0x1E, 0x94, 0x22}, 0x4000, 0x40, "ATtiny1614"},
	{{0x1E, 0x94, 0x24}, 0x4000, 0x40, "ATtiny1606"},
	{{0x1E, 0x94, 0x21}, 0x4000, 0x40, "ATtiny1616"},
	{{0x1E, 0x94, 0x23}, 0x4000, 0x40, "ATtiny1607"},
	{{0x1E, 0x94, 0x20}, 0x4000, 0x40, "ATtiny1617"},

	{{0x1E, 0x95, 0x20}, 0x8000, 0x80, "ATtiny3214"},
	{{0x1E, 0x95, 0x21}, 0x8000, 0x80, "ATtiny3216"},
	{{0x1E, 0x95, 0x22}, 0x8000, 0x80, "ATtiny3217"},
};

int crc16(const uint8_t* addr, int num, int crc = 0xFFFF)
{
	while (num--)
	{
		crc ^= *addr++ << 8;
		for (int i = 0; i < 8; i++)
		{
			crc = crc << 1;
			if (crc & 0x10000)
				crc ^= 0x11021;
		}
	}
	return crc;
}

class Stk500
{
	HANDLE m_Serial = INVALID_HANDLE_VALUE;
	bool m_Verbose = false;
	bool m_Connected = false;
	std::string m_Port;
	uint16_t m_FlashSize = 0;
	uint8_t m_PageSize = 0;
	
public:	
	~Stk500()
	{
		Close();
		if (m_Serial != NULL)
		{
			CloseHandle(m_Serial);
			m_Serial = NULL;
		}
	}

	bool Open(const char* comport, int baudrate = 500000, bool verbose = false)
	{
		m_Port = comport;
		HANDLE hSerial = CreateFileA((R"(\\.\)" + m_Port).c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

		m_Verbose = verbose;
		
		if (hSerial == INVALID_HANDLE_VALUE) 
		{
            fprintf(stderr, "Error: Could not open serial port.\n");
            return false;
		} 
		
		DCB dcbSerialParams = { sizeof(dcbSerialParams) };
		if (GetCommState(hSerial, &dcbSerialParams) == 0) 
		{
			fprintf(stderr, "Error getting device state\n");
			CloseHandle(hSerial);
			return false;
		}
		
		dcbSerialParams.BaudRate = baudrate; //CBR_115200;
		dcbSerialParams.ByteSize = 8;
		dcbSerialParams.StopBits = ONESTOPBIT;
		dcbSerialParams.Parity = NOPARITY;
		dcbSerialParams.fBinary = 1;

		if(SetCommState(hSerial, &dcbSerialParams) == 0) 
		{
			fprintf(stderr, "Error setting device parameters\n");
			CloseHandle(hSerial);
			return false;
		}
 
		COMMTIMEOUTS timeouts;
		timeouts.ReadIntervalTimeout = 250;
		timeouts.ReadTotalTimeoutConstant = 1000;
		timeouts.ReadTotalTimeoutMultiplier = 10;
		timeouts.WriteTotalTimeoutConstant = 100;
		timeouts.WriteTotalTimeoutMultiplier = 10;
		if (SetCommTimeouts(hSerial, &timeouts) == 0) 
		{
			fprintf(stderr, "Error setting timeouts\n");
			CloseHandle(hSerial);
			return false;
		}		        
		m_Serial = hSerial;
		Purge();

		return true;
	}
	
	int Available()
	{
		DWORD flags = 0;
        COMSTAT comstat;
		if (!ClearCommError(m_Serial, &flags, &comstat))
		{
            return 0;
		}
		return comstat.cbInQue;
	}
	
	int Read(void* buf, int bytes)
	{
		DWORD n = 0;
		if (ReadFile(m_Serial, buf, bytes, &n, NULL))
			return n;
		return 0;
	}
	int Read()
	{
		char c;
		if (Read(&c, 1))
			return c;
		return -1;
	}	
	int Write(const void* data, int len)
	{
		DWORD n = 0;
		if (WriteFile(m_Serial, data, len, &n, NULL))
			return n;
		return 0;
	}	
	int Write(const char* str)
	{
		return Write(str, (int)strlen(str));
	}
	int Write(char c)
	{
		return Write(&c, 1);
	}
		
	void Purge()
	{
		PurgeComm(m_Serial, PURGE_TXCLEAR | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_RXABORT);
	}
        
    bool Connect()
	{
		Purge();
		Write("0 ");
		for(int i = 0; i < 5; ++i)
		{
			Write("0 ");
			int r;
			for (;;)
			{
				r = -1;
				int c = Read();
				if (c == 0x14)
					r = Read();
				if (!Available())
					break;
			}
			if (r == 0x10)
			{
				uint8_t signature[3];
				Write("u ");
				int r = -1;
				for (; Read() == 0x14 && (r = Read()) == 0x10;);
				if (r >= 0 && Read(&signature[1], 2) == 2 && Read() == 0x10)
				{
					signature[0] = r;
					for (int i = 0; i < ARRAYSIZE(parts); ++i)
					{
						if (memcmp(signature, parts[i].signature, 3) == 0)
						{
							m_Connected = true;
							m_FlashSize = parts[i].flashSize;
							m_PageSize = parts[i].pageSize;
							printf("Connected to %s on %s\n", parts[i].name, m_Port.c_str());
							return true;
						}
					}
					printf("Unknown device %02X%02X%02X on %s\n", signature[0], signature[1], signature[2], m_Port.c_str());
					return false;
				}
				else
				{
					printf("Error reading remote device's signature\n");
					return false;
				}
			}
			if (r == 0x11)
			{
				printf("Error connecting to remote device\n");
				return false;
			}
		}
		printf("No response on %s\n", m_Port.c_str());
		return false;
	}

    bool CheckResponse()
	{
		Write(' ');
		int c = Read();
		if (c == 0x14)
		{
			c = Read();
			if (c == 0x10)
				return true;
			if (c == 0x11)
			{
				fprintf(stderr, "\nFailed flashing\n");
				return false;
			}
		}
		if (c < 0)
			fprintf(stderr, "\nTimed out waiting for response\n");
		else
			fprintf(stderr, "\nUnexpected response\n");
		
		return false;
	}
	
    bool Program(int segment, int start, std::vector<uint8_t> data, bool writeCrc = false)
	{
		char type;
		const char* name;
		int pagesize = 32;
		int size = (int)data.size();
        switch (segment)
		{
			case 0:
				type = 'F';
				name = "program memory";
				pagesize = m_PageSize;				
				if (writeCrc && start + size < m_FlashSize)
				{
					// provided the rest of flash is cleared to zeroes we
					// can just stick the CRC on the end of the program					
					uint16_t crc = crc16(&data[0], size);
					data.push_back(crc >> 8);
					data.push_back(crc & 255);
					// pad out to a full page with zeroes
					//data.resize((size + pagesize - 1) & ~(pagesize - 1), 0);
					data.resize(m_FlashSize - start, 0);
					size = (int)data.size();
				}
				break;
			case 0x81:
				type = 'E';
				name = "EEPROM";
				break;
			case 0x82:
				printf("Skipping fuses segment\n");
				return true;
			case 0x85:
				type = 'U';
				name = "user signatures";
				break;
			default:
				printf("Unknown segment 0x%08X-0x%08X\n", start, start + size);
				return false;
		}

        printf("Writing %i bytes to %s", size, name);
        for(int pos = 0; pos < size; pos += pagesize)
		{
            int addr = start + pos;
			int packetsize = std::min(pagesize, size - pos);
            fputc('.', stdout);
			fflush(stdout);
            Write(0x55);
			Write(addr & 255);
			Write((addr>>8)&255);
			if (!CheckResponse())
				return false;
            Write(0x64);
			Write('\0');
			Write(packetsize);
			Write(type);
			Write(&data[pos], packetsize);
			if (!CheckResponse())
				return false;
		}
        puts("OK\n");
		return true;
	}
        
    void Close()
	{
		if (m_Connected)
		{
			Write('Q');
			CheckResponse();
			m_Connected = false;
		}
	}
        
    bool SendCommand(const char* cmd)
	{
		Purge();
		Write(cmd);
		int c0 = -1;
		int c = Read();        
		while (c0 != '\n' || c != '>')
		{
            if (c < 0)
			{
				fprintf(stderr, "Communication error\n");
				return false;
			}
			if (m_Verbose)
				fputc(c, stdout);
			c0 = c;
            c = Read();
		}
		return true;
	}
};
    
int main(int argc, char* argv[])
{
	printf("STK500 flash tool\n");

	std::string port;
	std::string flash;
	std::string addr, setaddr;
	int baudrate = 500000;
	bool verbose = false;
	bool printHelp = false;
	bool crc = false;

	// First configure all possible command line options.
	CommandLine args("STK500 flash tool");
	args.addArgument({ "-c", "--comport" }, &port, "Com port to use");
	args.addArgument({ "-b", "--baudrate" }, &baudrate, "Baud rate (default 500000)");
	args.addArgument({ "-f", "--flash" }, &flash, "Intel HEX file to flash");
	args.addArgument({ "-a", "--addr" }, &addr, "Remote radio address");
	args.addArgument({ "-s", "--setaddr" }, &setaddr, "Reprogram remote radio address");
	args.addArgument({ "--crc" }, &crc, "Write CRC to end of flash");
	args.addArgument({ "-v", "--verbose" }, &verbose, "Verbose output");
	args.addArgument({ "-h", "--help" }, &printHelp, "Help!");

	try {
		args.parse(argc, argv);
	}
	catch (std::runtime_error const& e) {
		std::cout << e.what() << std::endl;
		return -1;
	}

	if (printHelp) 
	{
		args.printHelp();
		return 0;
	}

    Stk500 prog;
    if (!prog.Open(port.c_str(), baudrate, verbose))
		return 1;

	if (!addr.empty() || !setaddr.empty())
	{
		char buf[64];
		if (!prog.SendCommand("*cfg\n"))
			return 2;
		if (!addr.empty())
		{
			sprintf_s(buf, "id %s\n", addr.c_str());
			if (!prog.SendCommand(buf))
				return 2;
		}
		if (!setaddr.empty())
		{
			sprintf_s(buf, "setid %s\n", setaddr.c_str());
			if (!prog.SendCommand(buf))
				return 2;
		}
	}

	if (!flash.empty())
	{
		FILE* f = NULL;
		if (fopen_s(&f, flash.c_str(), "r"))
		{
			fprintf(stderr, "Error opening %s\n", flash.c_str());
			return 2;
		}
		if (!prog.Connect())
			return 1;
		std::vector<uint8_t> databuf;	
		char line[128];
		int segment = 0;
		int progaddr = 0;
		while (fgets(line, 128, f))
		{
			int bytes, address, type;
			if (sscanf_s(line, ":%02x%04x%02x", &bytes, &address, &type) == 3)
			{
				if (!databuf.empty() && (type != 0 || address != progaddr + databuf.size()))
				{
					if (!prog.Program(segment, progaddr, databuf, crc))
						return 1;
					databuf.clear();
				}
				if (type == 0)
				{
					if (databuf.empty())
						progaddr = address;
					int i;
					const char* hex = line + 9;
					for( i = 0; i < bytes; ++i, hex += 2)
					{
						int value = 0;
						if (sscanf_s(hex, "%02x", &value) == 1)
							databuf.push_back(value);
						else
							break;
					}
					if (i == bytes)
						continue;
				}
				int checksum;
				if (type == 4 && sscanf_s(line + 9, "%04x%02x", &segment, &checksum) == 2)
					continue;
				if (type == 1)
					break;
				if (type == 3)
					continue;
				fprintf(stderr, "Unknown record type %i in hex file\n", type);
				return 1;
			}
			else
			{
				fprintf(stderr, "Error parsing HEX file\n");
				return 1;
			}			
		}
	}
	prog.Close();
	//if (!prog.SendCommand("*cfg\n"))
	//	return 2;
	//if (!prog.SendCommand("crc\n"))
	//	return 2;
	//if (!prog.SendCommand("r\n"))
	//	return 2;
	printf("Done!");
	return 0;
}
