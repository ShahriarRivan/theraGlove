#include <WiFi.h>
#include <FirebaseESP32.h>
#include <Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <tensorflow/lite/version.h>

// Replace with your network credentials
const char* ssid = "Shahriar";      
const char* password = "fardinss11"; 

// Firebase configuration
#define API_KEY "AIzaSyDgvZFHAQeTBZYtS6OJ_lSvvosV_JN2sPc"
#define FIREBASE_AUTH_DOMAIN "webfitlo.firebaseapp.com"
#define FIREBASE_PROJECT_ID "webfitlo"
#define FIREBASE_STORAGE_BUCKET "webfitlo.appspot.com"
#define FIREBASE_MESSAGING_SENDER_ID "471149415032"
#define FIREBASE_APP_ID "1:471149415032:web:a05386b2d086e86f513b8d"

// Initialize Firebase objects
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

Servo servo1;
Servo servo2;

const int servoPin1 = 18;
const int servoPin2 = 19;

const int flexPins[5] = {34, 35, 32, 33, 25};
const int forcePins[5] = {39, 36, 4, 0, 2};

int flexValues[5] = {0};
int forceValues[5] = {0};

// ST7789 display pins and object
#define TFT_CS    5   // Chip select control pin
#define TFT_DC    16  // Data Command control pin
#define TFT_RST   17  // Reset pin (could connect to ESP32's EN/RST pin)

// Initialize ST7789 display (240x240 pixels)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// TensorFlow Lite configuration
constexpr int kTensorArenaSize = 2 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

// Placeholder for TensorFlow Lite model data
extern const unsigned char model_data[];
extern const int model_data_len;

// Micro interpreter
tflite::MicroInterpreter* interpreter;
TfLiteTensor* input;
TfLiteTensor* output;

// Variable to store servo force level (1 to 4)
int servoForceLevel = 1;

float mapForceSensor(int rawValue) {
  // Convert rawValue (0 - 4095) to force in Newtons (0 - 10 N)
  float force = (rawValue * 10.0) / 4095.0;
  return force;
}

float mapFlexSensor(int rawValue) {
  // Convert rawValue (0 - 4095) to angle in degrees (0 - 90 degrees)
  float angle = (rawValue * 90.0) / 4095.0;
  return angle;
}

void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin1);
  servo2.attach(servoPin2);

  for (int i = 0; i < 5; i++) {
    pinMode(flexPins[i], INPUT);
    pinMode(forcePins[i], INPUT);
  }

  // Initialize display
  tft.init(240, 240);  // Initialize with the size of your display
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(2);  // Adjust rotation as needed
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = "https://webfitlog-default-rtdb.firebaseio.com/";  // Firebase Realtime Database URL

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Set up TensorFlow Lite model
  tflite::MicroErrorReporter micro_error_reporter;
  tflite::AllOpsResolver resolver;

  const tflite::Model* model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema version does not match.");
    return;
  }

  static tflite::MicroInterpreter static_interpreter(
    model, resolver, tensor_arena, kTensorArenaSize, &micro_error_reporter);
  interpreter = &static_interpreter;

  interpreter->AllocateTensors();

  input = interpreter->input(0);
  output = interpreter->output(0);
}

void loop() {
  // Read flex sensors
  for (int i = 0; i < 5; i++) {
    int rawFlex = analogRead(flexPins[i]);
    flexValues[i] = rawFlex;
  }

  // Read force sensors
  for (int i = 0; i < 5; i++) {
    int rawForce = analogRead(forcePins[i]);
    forceValues[i] = rawForce;
  }

  // Prepare data for TensorFlow Lite model
  for (int i = 0; i < 5; i++) {
    input->data.f[i] = mapForceSensor(forceValues[i]);
  }

  // Run inference
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Error running inference");
    return;
  }

  // Get prediction result
  servoForceLevel = static_cast<int>(output->data.f[0]);

  int servoAngle1 = map(servoForceLevel, 1, 4, 60, 120);
  int servoAngle2 = servoAngle1;
  servo1.write(servoAngle1);
  servo2.write(servoAngle2);

  displayReadings();

  // Prepare data in JSON format
  FirebaseJson json;
  FirebaseJsonArray flexArray;
  for (int i = 0; i < 5; i++) {
    flexArray.add(flexValues[i]);
  }
  json.set("flexValues", flexArray);

  FirebaseJsonArray forceArray;
  for (int i = 0; i < 5; i++) {
    forceArray.add(forceValues[i]);
  }
  json.set("forceValues", forceArray);
  json.set("servoForceLevel", servoForceLevel);

  // Generate a unique key based on timestamp
  String timestamp = String(millis());
  String path = "/patients/patient1/session1/" + timestamp;

  // Push data to Firebase
  if (Firebase.RTDB.setJSON(&firebaseData, path.c_str(), &json)) {
    Serial.println("Data pushed successfully");
  } else {
    Serial.print("Failed to push data: ");
    Serial.println(firebaseData.errorReason());
  }

  delay(1000);
}

void displayReadings() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);

  tft.println("Force Sensor Readings:");
  for (int i = 0; i < 5; i++) {
    tft.print("F");
    tft.print(i + 1);
    tft.print(": ");
    tft.println(forceValues[i]);
  }

  tft.println();
  tft.println("Flex Sensor Readings:");
  for (int i = 0; i < 5; i++) {
    tft.print("F");
    tft.print(i + 1);
    tft.print(": ");
    tft.println(flexValues[i]);
  }

  tft.println();
  tft.print("Servo Force Level: ");
  tft.println(servoForceLevel);
}
