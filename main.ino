
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

uint8_t display_draw_time=20;
PxMATRIX display(matrix_width, matrix_height, P_LAT, P_OE,P_A,P_B,P_C,P_D);
hw_timer_t * timer { nullptr };
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
AsyncUDP udp;
WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);
TaskHandle_t UpdateTask;

#define PxMATRIX_DOUBLE_BUFFER True

static const PROGMEM uint8_t lightPowerMap8bit[256] = {
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11,
	11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17,
	17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24,
	25, 25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32, 32, 33, 34,
	34, 35, 36, 37, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46,
	47, 47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77,
	79, 80, 81, 82, 83, 85, 86, 87, 88, 90, 91, 92, 94, 95, 96, 98,
	99, 100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121,
	123, 124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148,
	150, 152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179,
	181, 183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214,
	216, 218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252
};


void IRAM_ATTR display_updater(){
	// Increment the counter and set the time of ISR
	portENTER_CRITICAL_ISR(&timerMux);
	display.display(display_draw_time);
	portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable)
{
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

void notify(const char *text)
{
	display.setTextColor(display.color565(255, 0, 0));
	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(text);
}

void scroll_text(uint8_t ypos, unsigned long scroll_delay, String text, uint8_t colorR, uint8_t colorG, uint8_t colorB)
{
	uint16_t text_length = text.length();
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

void scroll_text_char(uint8_t ypos, unsigned long scroll_delay, char* text, uint16_t text_length,
		uint8_t colorR, uint8_t colorG, uint8_t colorB)
{
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
		String clientId = "esp32-pixel-";
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
					// display.drawPixelRGB888(x, y, lightPowerMap8bit[r], lightPowerMap8bit[g], lightPowerMap8bit[b]);
					display.drawPixelRGB888(x, y, r, g, b);
				}
			});
		display.showBuffer();
	}
}

void loop() {
	if (!mqttClient.connected()) {
		mqtt_reconnect(); 

		if (mqttClient.connected()) {
			mqttClient.publish("doorpixel/hello-version", __DATE__ " " __TIME__);

			String temp = WiFi.localIP().toString();
			mqttClient.publish("doorpixel/hello-ip", temp.c_str());
		}
	}

	mqttClient.loop();
}
