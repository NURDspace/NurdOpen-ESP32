
#include <PxMatrix.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>

#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 2
#define matrix_width 64
#define matrix_height 32
#define MAX_PIXELS 140
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

AsyncUDP udp;
TaskHandle_t UpdateTask;
WiFiClient espMqttClient;
uint8_t display_draw_time=20;
hw_timer_t * timer { nullptr };
PubSubClient mqttClient(espMqttClient);
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
PxMATRIX display(matrix_width, matrix_height, P_LAT, P_OE,P_A,P_B,P_C,P_D);

#define PxMATRIX_DOUBLE_BUFFER True

void IRAM_ATTR display_updater() {
	// Increment the counter and set the time of ISR
	portENTER_CRITICAL_ISR(&timerMux);
	display.display(display_draw_time);
	portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable) {
	if (is_enable) {
		timerAttachInterrupt(timer, &display_updater, true);
		timerAlarmWrite(timer, 4000, true);
		timerAlarmEnable(timer);
	}
	else {
		timerDetachInterrupt(timer);
		timerAlarmDisable(timer);
	}
}

void notify(const char *text) {
	display.setTextColor(display.color565(255, 0, 0));
	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(text);
}

void scroll_text(uint8_t ypos, unsigned long scroll_delay, String text, uint8_t colorR, uint8_t colorG, uint8_t colorB){
	uint16_t text_length = text.length();
	display.setTextWrap(false);  // we don't wrap text so it scrolls nicely
	display.setTextSize(2);
	display.setRotation(0);
	display.setTextColor(display.color565(colorR,colorG,colorB));
	Serial.println(text);

	// Asuming 5 pixel average character width
	for (int xpos=matrix_width; xpos>-(matrix_width+text_length*10); xpos--) {
		display.setTextColor(display.color565(colorR,colorG,colorB));
		display.clearDisplay();
		display.setCursor(xpos,ypos);
		display.println(text);

		delay(scroll_delay);
		yield();
		delay(scroll_delay/5);
		yield();
	}
}

void scroll_text_char(uint8_t ypos, unsigned long scroll_delay, char* text, uint16_t text_length,
		uint8_t colorR, uint8_t colorG, uint8_t colorB) {
	display.setTextWrap(false);  // we don't wrap text so it scrolls nicely
	display.setTextSize(2);
	display.setRotation(0);
	display.setTextColor(display.color565(colorR,colorG,colorB));
	Serial.println(text);

	// Asuming 5 pixel average character width
	for (int xpos=matrix_width; xpos>-(matrix_width+text_length*10); xpos--)
	{
		display.setTextColor(display.color565(colorR,colorG,colorB));
		display.clearDisplay();
		display.setCursor(xpos,ypos);
		display.println(text);
		delay(scroll_delay);
		yield();

		delay(scroll_delay/5);
		yield();
	}
}

void initWiFi() {
	display_update_enable(true);
	notify("init wifi");
	delay(1000);

	// keep screen on & search for wifi gives strange reproducable timing related crashes
	display_update_enable(false);

	WiFi.begin("NurdSpace", "harkharkhark");

	digitalWrite(LED_BUILTIN, HIGH);

	while (WiFi.status() != WL_CONNECTED) {
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

		Serial.print(".");
		delay(200);
	}

	digitalWrite(LED_BUILTIN, LOW);

	notify("connected");
	Serial.println(WiFi.localIP());
}

void mqtt_reconnect() {
	digitalWrite(LED_BUILTIN, HIGH);
	int fail_count = 0;

	// Loop until we're reconnected
	while (!mqttClient.connected()) {
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

		if (++fail_count > 5)
			ESP.restart();

		notify("mqtt?");
		Serial.print("Attempting MQTT connection...");

		// Create a random client ID
		String clientId = "doorpixel-";
		clientId += String(random(0xffff), HEX);

		// Attempt to connect
		if (mqttClient.connect(clientId.c_str())) {
			Serial.println("connected");
			notify("mqtt!");
			mqttClient.subscribe("doorpixel/scroll");

		} else {
			notify("mqtt-");
			Serial.print("failed, rc=");
			Serial.print(mqttClient.state());
			Serial.println(" try again in 0.1 second");
			delay(100);
		}
	}

	digitalWrite(LED_BUILTIN, LOW);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	char buffer[512];

	if (length > 508)
		length = 508; // some space for spaces :-)

	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
		buffer[i] =(char)payload[i];
	}

	buffer[length++] = ' ';
	buffer[length++] = ' ';
	buffer[length++] = ' ';
	buffer[length] = 0x00;
	Serial.println();

	if (strcmp(topic, "doorpixel/scroll") == 0)
		scroll_text_char(0, 30, buffer, length, 0, 255, 0);
}

void setup() {
	Serial.begin(115200);
	Serial.println(__DATE__ " " __TIME__);
	pinMode(LED_BUILTIN, OUTPUT);
	
	timer = timerBegin(0, 80, true);

	display.setBrightness(128);
	display.begin(16);
	display.clearDisplay();

	WiFi.setSleep(false);
	initWiFi();
	
	display_update_enable(true);
	scroll_text(0, 30, WiFi.localIP().toString(), 0, 255, 0);

	ArduinoOTA.setHostname("doorpixel");
	
	ArduinoOTA.onStart([]() {
    	Serial.println("Start");
 	 });
	
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
  
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    	Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  	});
  	
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});

  	ArduinoOTA.begin();
	mqttClient.setServer("mqtt.vm.nurd.space", 1883);
	mqttClient.setCallback(mqtt_callback);

	if (udp.listen(5004)) {
		Serial.println("UDP initialized");

		udp.onPacket([](AsyncUDPPacket packet) {

				int inc = packet.data()[1] ? 8 : 7;
				for(int i=2; i<packet.length(); i += inc) {
					int x = (packet.data()[i + 1] << 8) | packet.data()[i + 0];
					int y = (packet.data()[i + 3] << 8) | packet.data()[i + 2];
					int r = packet.data()[i + 4];
					int g = packet.data()[i + 5];
					int b = packet.data()[i + 6];
					display.drawPixelRGB888(x, y, r, g, b);
				}
			}

		);
		display.showBuffer();
	}
}

void loop() {
	if (!mqttClient.connected()) {
		initWiFi();
		mqtt_reconnect(); 
		
		if (mqttClient.connected()) {
			mqttClient.publish("doorpixel/hello-version", __DATE__ " " __TIME__);

			String temp = WiFi.localIP().toString();
			mqttClient.publish("doorpixel/hello-ip", temp.c_str());
		}
	}

	mqttClient.loop();
	ArduinoOTA.handle();
}
