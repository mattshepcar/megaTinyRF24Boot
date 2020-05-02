#include <megaTinyNrfConsole.h>
#ifdef ESP8266
#include <WiFiManager.h>
#include <ArduinoOTA.h>
WiFiServer Server(1614);
WiFiClient Client;
#endif

#ifdef ESP8266
#define NRF24_CSN_PIN D8
#define NRF24_CE_PIN D0
#else
#define NRF24_CSN_PIN PIN_PB0
#define NRF24_CE_PIN PIN_PB1
#endif

mtnrf::Radio Radio(NRF24_CE_PIN, NRF24_CSN_PIN);
mtnrf::BootLoader BootLoader(Radio);
mtnrf::Console Console(BootLoader);

void setup()
{
	delay(50);
	Serial.begin(500000);
	Serial.println(F("nRF24L01+ ATtiny 0/1 programming bridge"));

#ifdef ESP8266
	WiFiManager wifiManager;
	wifiManager.autoConnect("nrf24prog");
	ArduinoOTA.setHostname("nrf24prog");
	ArduinoOTA.begin();
	Server.begin();
#endif

	mtnrf::Config config("001", 3, 50, mtnrf::RF24_2MBPS);
	config.setRetries(0, 15, 16);
	while (!Radio.begin(config))
	{
		Serial.println("radio not connected");
		delay(500);
	}
	Console.begin(Serial);
}

void loop()
{
#ifdef ESP8266
	ArduinoOTA.handle();
	if (!Client.connected())
	{
		Client = Server.available();
		if (Client)
		{
			Client.println(F("ESP8266 nRF24L01+ ATtiny 0/1 programming bridge"));
			Console.begin(Client);
		}
		else if (Console.getStream() != &Serial)
		{
			Console.begin(Serial);
		}
	}
#endif

	Console.handle();
}
