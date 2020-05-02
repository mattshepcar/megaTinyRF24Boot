#include "CommandLine.hpp"
#include <algorithm>
#include "Platform.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "wsock32")
#pragma comment(lib, "ws2_32")

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
	bool m_IsSocket = false;
	bool m_Verbose = false;
	bool m_Connected = false;
	std::string m_Port;
	uint16_t m_FlashSize = 0;
	uint8_t m_PageSize = 0;
	int m_PendingResponseData = 0;

public:	
	~Stk500()
	{
		Close();
		if (m_Serial != NULL)
		{
			if (m_IsSocket)
			{
				closesocket((SOCKET) m_Serial);
				WSACleanup();
			}
			else
			{
				CloseHandle(m_Serial);
			}
			m_Serial = NULL;
		}
	}

	bool Open(const char* addr, const char* port)
	{
		m_Port = addr;
		m_Port += ":";
		m_Port += port;
		WSADATA wsadata = { 0 };
		int result = WSAStartup(MAKEWORD(2, 2), &wsadata);
		if (result != 0)
		{
			printf("Error opening winsock: %d\n", result);
			return false;
		}
		DWORD flags = 0;
		GROUP group = 0;
		WSAPROTOCOL_INFO* protoInfo = nullptr;

		struct addrinfo hints = { 0 };
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		struct addrinfo* addresses = nullptr;
		result = getaddrinfo(addr, port, &hints, &addresses);
		if (result != 0) 
		{
			printf("getaddrinfo failed with error: %d\n", result);
			WSACleanup();
			return false;
		}

		SOCKET hSocket = INVALID_SOCKET;
		for (struct addrinfo* ptr = addresses; ptr; ptr = ptr->ai_next) 
		{
			hSocket = WSASocket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol, protoInfo, group, flags);
			if (hSocket == INVALID_SOCKET)
			{
				printf("socket failed with error: %ld\n", WSAGetLastError());
				break;
			}
			result = connect(hSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
			if (result != SOCKET_ERROR)
			{
				break;
			}
			closesocket(hSocket);
			hSocket = INVALID_SOCKET;
		}
		freeaddrinfo(addresses);

		if (hSocket == INVALID_SOCKET)
		{
			WSACleanup();
			return false;
		}

		
		//DWORD sndbuf = 32;
		//result = setsockopt(hSocket, SOL_SOCKET, SO_SNDBUF, (char*) &sndbuf, sizeof(DWORD));
		//if (result == SOCKET_ERROR)
		//	return false;
		//DWORD rcvbuf = 32;
		//result = setsockopt(hSocket, SOL_SOCKET, SO_RCVBUF, (char*) &rcvbuf, sizeof(DWORD));
		//if (result == SOCKET_ERROR)
		//	return false;
		//DWORD rcvtimeo = 100;
		//result = setsockopt(hSocket, SOL_SOCKET, SO_RCVTIMEO, (char*) &rcvtimeo, sizeof(DWORD));
		//if (result == SOCKET_ERROR)
		//	return false;
		//result = setsockopt(hSocket, SOL_SOCKET, SO_SNDTIMEO, (char*) &rcvtimeo, sizeof(DWORD));
		//if (result == SOCKET_ERROR)
		//	return false;		
		//BOOL disable = TRUE;
		//setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (char*) &disable, sizeof(BOOL));

		m_Serial = (HANDLE)hSocket;
		m_IsSocket = true;
		return true;
	}

	void SetVerbose(bool verbose) { m_Verbose = verbose; }

	bool Open(const char* comport, int baudrate = 500000)
	{
		m_Port = comport;
		HANDLE hSerial = CreateFileA((R"(\\.\)" + m_Port).c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
				
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
		m_IsSocket = false;
		Purge();

		return true;
	}
	
	int Available()
	{
		if (m_IsSocket)
		{
			FD_SET sockets;
			FD_ZERO(&sockets);
			FD_SET((SOCKET)m_Serial, &sockets);
			TIMEVAL timeout = { 0 };
			return select(1, &sockets, nullptr, nullptr, &timeout) == 1 ? 1 : 0;
		}
		else
		{
			DWORD flags = 0;
			COMSTAT comstat;
			if (!ClearCommError(m_Serial, &flags, &comstat))
				return 0;
			return comstat.cbInQue;
		}
	}
	
	int Read(void* buf, int bytes)
	{
		DWORD totalRead = 0;
		while (totalRead < bytes)
		{
			if (m_IsSocket)
			{
				FD_SET sockets;
				FD_ZERO(&sockets);
				FD_SET((SOCKET) m_Serial, &sockets);
				TIMEVAL timeout = { 0 };
				timeout.tv_sec = 10;
				if (select(1, &sockets, nullptr, nullptr, &timeout) != 1)
				{
					return totalRead;
				}
			}
			DWORD n = 0;
			if (!ReadFile(m_Serial, (uint8_t*)buf + totalRead, bytes - totalRead, &n, NULL))
				return totalRead;
			totalRead += n;
		}
		return totalRead;
	}
	int Read()
	{
		char c;
		if (Read(&c, 1))
		{
			//printf("<%02x ", c);
			return c;
		}
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
		//printf(">%02x ", c);
		return Write(&c, 1);
	}
		
	void Purge()
	{
		while (Available() > 0)
		{
			int c = Read();
			if (m_Verbose && (c == '\n' || c == '\r' || c >= 32 && c < 128))
				fputc(c, stdout);
		}
		if (!m_IsSocket)
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
				if (c == 0x14 || c < 0)
				{
					r = Read();
					break;
				}
			}
			if (r == 0x10)
			{
				if (Read() == 0x14)
					Read();
				Purge();

				uint8_t sigbuf[5];
				Write("u ");
				if (Read(sigbuf, 5) == 5 && sigbuf[0] == 0x14 && sigbuf[4] == 0x10)
				{
					for (int i = 0; i < ARRAYSIZE(parts); ++i)
					{
						if (memcmp(sigbuf + 1, parts[i].signature, 3) == 0)
						{
							m_Connected = true;
							m_FlashSize = parts[i].flashSize;
							m_PageSize = parts[i].pageSize;
							printf("Connected to %s on %s\n", parts[i].name, m_Port.c_str());
							return true;
						}
					}
					printf("Unknown device %02X%02X%02X on %s\n", sigbuf[1], sigbuf[2], sigbuf[3], m_Port.c_str());
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

	bool CheckResponse(bool blocking = true)
	{		
		for (; m_PendingResponseData > 0 && (blocking || Available()); --m_PendingResponseData)
		{
			int resp = Read();
			int expected = m_PendingResponseData & 1 ? 0x10 : 0x14;
			if (resp != expected)
			{
				if (resp < 0)
					fprintf(stderr, "\nTimed out waiting for response\n");
				else if ((m_PendingResponseData & 1) && resp == 0x11)
					fprintf(stderr, "\nFailed flashing\n");
				else
					fprintf(stderr, "\nUnexpected response 0x%02x\n", resp);
				Purge();
				return false;
			}
			if (resp == 0x10)
			{
				fputc('.', stdout);
				//fflush(stdout);
			}
		}
		return true;
	}

	bool Program(int segment, int start, std::vector<uint8_t> data)
	{
		uint8_t type;
		const char* name;
		int pagesize = 32;
		int size = (int)data.size();
        switch (segment)
		{
			case 0:
				type = 'F';
				name = "program memory";
				pagesize = m_PageSize;				
				if (start + size < m_FlashSize)
				{
					// provided the rest of flash is cleared to zeroes we
					// can just stick the CRC on the end of the program					
					uint16_t crc = crc16(&data[0], size);
					data.push_back(crc >> 8);
					data.push_back(crc & 255);
					// pad out to a full page with zeroes
					data.resize((size + pagesize - 1) & ~(pagesize - 1), 0);
					//data.resize(m_FlashSize - start, 0);
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
			uint8_t packetsize = std::min(pagesize, size - pos);
			uint8_t packet [] = 
			{
				0x55, (uint8_t)(addr & 255), (uint8_t)(addr >> 8), ' ',
				0x64, 0, packetsize, type
			};
			Write(packet, sizeof(packet));
			Write(&data[pos], packetsize);
			Write(' ');
			m_PendingResponseData += 4;
			if (!CheckResponse(false))
				return false;
		}
		if (!CheckResponse())
			return false;
		puts("OK\n");
		return true;
	}
        
    void Close()
	{
		if (m_Connected)
		{
			Write("Q ");
			m_PendingResponseData += 2;
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

	std::string comport;
	std::string flash;
	std::string addr, setaddr;
	std::string ip, port;
	int baudrate = 500000;
	bool verbose = false;
	bool printHelp = false;
	bool crc = false;

	// First configure all possible command line options.
	CommandLine args("STK500 flash tool");
	args.addArgument({ "-c", "--comport" }, &comport, "Com port to use");
	args.addArgument({ "-i", "--ip" }, &ip, "IP address to use");
	args.addArgument({ "-p", "--port" }, &port, "TCP port to use");
	args.addArgument({ "-b", "--baudrate" }, &baudrate, "Baud rate (default 500000)");
	args.addArgument({ "-f", "--flash" }, &flash, "Intel HEX file to flash");
	args.addArgument({ "-a", "--addr" }, &addr, "Remote radio address");
	args.addArgument({ "-s", "--setaddr" }, &setaddr, "Reprogram remote radio address");
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
	if (!ip.empty())
	{
		if (!prog.Open(ip.c_str(), port.c_str()))
			return 1;
	}
	else if (!prog.Open(comport.c_str(), baudrate))
	{
		return 1;
	}

	if (!prog.SendCommand("*cfg\n"))
		return 2;

	if (verbose)
	{
		if (!prog.SendCommand("v\n"))
			return 2;
		prog.SetVerbose(true);
	}

	char buf[64];
	if (!addr.empty())
	{
		sprintf_s(buf, "addr %s\n", addr.c_str());
		if (!prog.SendCommand(buf))
			return 2;
	}
	if (!setaddr.empty())
	{
		sprintf_s(buf, "setid %s\n", setaddr.c_str());
		if (!prog.SendCommand(buf))
			return 2;
	}
	if (!prog.Write("q\n"))
		return 2;

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
					if (!prog.Program(segment, progaddr, databuf))
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
