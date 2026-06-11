// ==========================================
//   SMART TRAFFIC LIGHT PARKING ASSISTANT
//   (TF-LUNA LiDAR - ULTIMATE EDITION)
// ==========================================
// Designed for Arduino Uno/Nano, but safely compiles on any board!
// 
// Wiring Guide:
// - Green Relay  -> Pin 2
// - Yellow Relay -> Pin 3
// - Red Relay    -> Pin 4
// - Mode Switch  -> Pin 12 (to Ground)
// - TF-Luna RX   -> Pin 10
// - TF-Luna TX   -> Pin 9 (Leave UNPLUGGED to protect 3.3v sensor!)
// ==========================================

#include <SoftwareSerial.h>

// --- CUSTOM RELAY LOGIC ---
// Translates human logic to "Active-Low" relay logic
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// --- PIN DEFINITIONS ---
const int greenLight = 2;
const int yellowLight = 3;
const int redLight = 4;
const int modeSwitch = 12;

// Set up the LiDAR serial communication (RX on Pin 10, TX on Pin 9)
SoftwareSerial lidarSerial(10, 9); 

// --- PARKING DISTANCES (Inches) ---
// Adjusted for 22.5ft wake-up, reaction time buffers, and a 21" wall shelf
const int idleDistance = 270;   // <= 270" (22.5 ft) = GREEN turns on
const int slowDistance = 120;   // <= 120" (10 ft)   = YELLOW
const int stopDistance = 48;    // <= 48"  (4 ft)    = SOLID RED (Perfect Park)
const int dangerDistance = 36;  // <= 36"  (3 ft)    = FLASHING RED (15" from shelf!)

// --- STARTUP & LOGIC VARIABLES ---
bool firstScanDone = false;     
bool carAlreadyHere = false;    
int parkedCounter = 0;          
int lastKnownDistance = 999;    

// --- ANTI-FLICKER & TIMEOUT TRACKERS ---
int currentState = 0; // 0=Standby, 1=Green, 2=Yellow, 3=Red, 4=Flashing Red
int summerCycleCount = 0; // Tracks the number of Summer Mode loops

void setup() {
  pinMode(greenLight, OUTPUT);
  pinMode(yellowLight, OUTPUT);
  pinMode(redLight, OUTPUT);
  pinMode(modeSwitch, INPUT_PULLUP); 
  
  Serial.begin(9600); 
  lidarSerial.begin(115200); 
  
  Serial.println("LiDAR System Starting...");

  digitalWrite(greenLight, RELAY_OFF);
  digitalWrite(yellowLight, RELAY_OFF);
  digitalWrite(redLight, RELAY_OFF);
  
  // ---------------------------------------------------------
  // SYSTEM BOOT LIGHT SHOW
  // ---------------------------------------------------------
  // Step 1: Sequential Cycle
  digitalWrite(greenLight, RELAY_ON); delay(750); digitalWrite(greenLight, RELAY_OFF);
  digitalWrite(yellowLight, RELAY_ON); delay(750); digitalWrite(yellowLight, RELAY_OFF);
  digitalWrite(redLight, RELAY_ON); delay(750); digitalWrite(redLight, RELAY_OFF);
  
  // Step 2: All OFF (Brief pause)
  delay(500); 

  // Step 3: All ON (The visual confirmation)
  // 
  // WHY USE PORTD? 
  // digitalWrite() executes sequentially. When firing three heavy mechanical 
  // relays, that micro-delay causes a noticeable, staggered "ka-ka-clack" sound,
  // and you can visually see the lights turn on one after the other. 
  // Direct Port Manipulation (PORTD) bypasses the software and flips the hardware 
  // register directly. This forces Pins 2, 3, and 4 to fire simultaneously on the 
  // exact same CPU clock cycle, resulting in a single, unified "snap" and a 
  // perfectly synchronized visual flash.
  // The #if directive ensures this only runs on compatible Uno/Nano boards, 
  // safely falling back to digitalWrite() for ESP32/Mega boards.
  #if defined(__AVR_ATmega328P__)
    PORTD &= ~B00011100; 
  #else
    digitalWrite(greenLight, RELAY_ON);
    digitalWrite(yellowLight, RELAY_ON);
    digitalWrite(redLight, RELAY_ON);
  #endif
  
  delay(1000); 
  
  // Step 4: All OFF & Ready
  #if defined(__AVR_ATmega328P__)
    PORTD |= B00011100; 
  #else
    digitalWrite(greenLight, RELAY_OFF);
    digitalWrite(yellowLight, RELAY_OFF);
    digitalWrite(redLight, RELAY_OFF);
  #endif
  
  delay(250); 
}

void loop() {
  if (digitalRead(modeSwitch) == LOW) {
    runParkingMode();
  } else {
    runSummerMode();
  }
}

// ---------------------------------------------------------
// LiDAR SENSOR READING FUNCTION (WITH CHECKSUM)
// ---------------------------------------------------------
void updateLidarDistance() {
  // Wait until we have a full 9-byte frame waiting in the buffer
  while (lidarSerial.available() >= 9) {
    
    // Check for Header Byte 1
    if (lidarSerial.read() == 0x59) {       
      // Check for Header Byte 2 (using peek so we don't accidentally consume data)
      if (lidarSerial.peek() == 0x59) {     
        lidarSerial.read(); // Consume Header Byte 2
        
        int uart[9];
        uart[0] = 0x59;
        uart[1] = 0x59;
        int checksum = uart[0] + uart[1];
        
        // Read the remaining 7 bytes of the payload
        for(int i = 2; i < 9; i++) {
           uart[i] = lidarSerial.read();
           if (i < 8) {
             checksum += uart[i]; // Add up the payload for the checksum
           }
        }
        
        // VERIFY CHECKSUM: Only update the distance if the math is perfect!
        if (uart[8] == (checksum & 0xFF)) {
          long distCm = uart[2] + (uart[3] << 8);
          int calculatedInches = distCm / 2.54; 
          
          // Force errors or infinite sky to 999 (Standby)
          if (calculatedInches <= 2 || calculatedInches > 500) {
            lastKnownDistance = 999;
          } else {
            lastKnownDistance = calculatedInches; 
          }
        }
      }
    }
  }
}

// ==========================================
// MODE 1: WINTER PARKING ASSISTANT 
// ==========================================
void runParkingMode() {
  // Reset the Summer Mode timeout counter anytime we switch back to Parking Mode
  summerCycleCount = 0; 

  updateLidarDistance(); 
  
  // WAKE-UP LOOK 
  if (!firstScanDone) {
    if (lastKnownDistance < slowDistance) {
      carAlreadyHere = true;
      Serial.println("WAKE-UP: Car is parked. Lights OFF for backing out.");
    }
    firstScanDone = true; 
    delay(10);
    return;
  }

  // BACKING OUT BYPASS
  if (carAlreadyHere) {
    digitalWrite(greenLight, RELAY_OFF);
    digitalWrite(yellowLight, RELAY_OFF);
    digitalWrite(redLight, RELAY_OFF);
    
    // System re-arms as soon as car clears the 10ft Yellow threshold
    if (lastKnownDistance > slowDistance) {
      Serial.println("Car left. System ARMED.");
      carAlreadyHere = false; 
    }
    delay(100); 
    return; 
  }
  
  if (lastKnownDistance > stopDistance) {
    parkedCounter = 0; 
  }
  
  // ---------------------------------------------------------
  // NORMAL PARKING LIGHT RULES (WITH HYSTERESIS)
  // ---------------------------------------------------------
  // Adds a 4-inch "sticky buffer" to the thresholds so vibrating 
  // on a boundary line doesn't make the lights flicker.
  int effIdle = idleDistance + (currentState >= 1 ? 4 : 0);
  int effSlow = slowDistance + (currentState >= 2 ? 4 : 0);
  int effStop = stopDistance + (currentState >= 3 ? 4 : 0);
  int effDanger = dangerDistance + (currentState >= 4 ? 4 : 0);

  Serial.print("LiDAR Distance: ");
  Serial.print(lastKnownDistance);
  Serial.print(" inches  --->  ");
  
  if (lastKnownDistance > effIdle) {
    Serial.println("STANDBY (All Off)"); 
    digitalWrite(greenLight, RELAY_OFF);
    digitalWrite(yellowLight, RELAY_OFF);
    digitalWrite(redLight, RELAY_OFF);
    currentState = 0; // Reset state
  }
  else if (lastKnownDistance <= effIdle && lastKnownDistance > effSlow) {
    Serial.println("GREEN");
    digitalWrite(greenLight, RELAY_ON);
    digitalWrite(yellowLight, RELAY_OFF);
    digitalWrite(redLight, RELAY_OFF);
    currentState = 1; // State: Green
  }
  else if (lastKnownDistance <= effSlow && lastKnownDistance > effStop) {
    Serial.println("YELLOW");
    digitalWrite(yellowLight, RELAY_ON);
    digitalWrite(redLight, RELAY_OFF);
    digitalWrite(greenLight, RELAY_OFF);
    currentState = 2; // State: Yellow
  } 
  else {
    parkedCounter++; 
    
    if (parkedCounter > 100) { 
      Serial.println("PARKED & DONE (All Off)");
      digitalWrite(redLight, RELAY_OFF);
      digitalWrite(yellowLight, RELAY_OFF);
      digitalWrite(greenLight, RELAY_OFF);
    } 
    else if (lastKnownDistance <= effDanger) {
      Serial.println("FLASHING RED (DANGER!)");
      digitalWrite(yellowLight, RELAY_OFF);
      digitalWrite(greenLight, RELAY_OFF);
      digitalWrite(redLight, RELAY_ON); 
      delay(300);                   
      digitalWrite(redLight, RELAY_OFF);  
      delay(200);                   
      currentState = 4; // State: Flashing Red
    } 
    else {
      Serial.println("SOLID RED"); 
      digitalWrite(redLight, RELAY_ON);
      digitalWrite(yellowLight, RELAY_OFF);
      digitalWrite(greenLight, RELAY_OFF);
      currentState = 3; // State: Solid Red
    }
  }
  
  delay(100); 
}

// ==========================================
// MODE 2: SUMMER TRAFFIC LIGHT
// ==========================================
void runSummerMode() {
  // TIMEOUT PROTECTOR: Stop after 300 cycles (approx 2 hours) to save the relays
  if (summerCycleCount >= 300) {
    digitalWrite(greenLight, RELAY_OFF);
    digitalWrite(yellowLight, RELAY_OFF);
    digitalWrite(redLight, RELAY_OFF);
    delay(1000); 
    return; // Exits the function early, locking the lights off
  }

  summerCycleCount++; // Add 1 to the loop counter
  
  Serial.print("SUMMER MODE CYCLE: ");
  Serial.print(summerCycleCount);
  Serial.println(" / 300");

  digitalWrite(greenLight, RELAY_ON);
  digitalWrite(yellowLight, RELAY_OFF);
  digitalWrite(redLight, RELAY_OFF);
  delay(10000); 
  
  digitalWrite(greenLight, RELAY_OFF);
  digitalWrite(yellowLight, RELAY_ON);
  digitalWrite(redLight, RELAY_OFF);
  delay(4000); 
  
  digitalWrite(greenLight, RELAY_OFF);
  digitalWrite(yellowLight, RELAY_OFF);
  digitalWrite(redLight, RELAY_ON);
  delay(10000); 
}