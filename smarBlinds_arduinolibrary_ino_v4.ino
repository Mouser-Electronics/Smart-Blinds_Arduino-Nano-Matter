#include <Matter.h>
#include <MatterWindowCovering.h>
#include <MatterTemperature.h>
#include <TMCStepper.h>  // Include TMCStepper library for controlling TMC2209

// Pin definitions
#define EN_PIN 7           // Enable pin (EN)
#define DIR_PIN 4          // Direction pin (DIR)
#define STEP_PIN 3         // Step pin (STEP)
#define R_SENSE 0.11       // Sense resistor value (typically 0.11 ohms for TMC2209)
#define UART_ADDRESS 0x00  // UART address for the TMC2209

// TMC2209 object
TMC2209Stepper driver(&Serial1, R_SENSE, UART_ADDRESS);  // Using Serial1 for UART communication

MatterWindowCovering matter_blinds;

// Constants for rolling curtain
const int stepsForFullRoll = 500;  // Number of steps for a full roll (adjust based on actual measurements)
const int movementMultiplier = 1;  // Move the motor 20 times more than the usual roll

void update_onboard_led(uint8_t brightness);

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);  // UART for TMC2209 communication

  Matter.begin();
  matter_blinds.begin();

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D2, OUTPUT);  // Initialize D2 as an output

  pinMode(STEP_PIN, OUTPUT);  // Step pin for TMC2209
  pinMode(DIR_PIN, OUTPUT);   // Direction pin for TMC2209
  pinMode(EN_PIN, OUTPUT);    // Enable pin for TMC2209

  // Disable the TMC2209 motor driver
  digitalWrite(EN_PIN, HIGH);  // LOW to disable motor driver

  // Initialize TMC2209 driver
  driver.begin();
  driver.toff(5);                // Enable driver with toff = 5
  driver.rms_current(500);       // Set RMS current to 500mA to limit current draw
  driver.microsteps(1);          // Set microstepping to 1 (full steps)
  driver.en_spreadCycle(false);  // Enable stealthChop for silent operation

  // Set idle current to a lower value (e.g., 200mA)
  driver.irun(31);  // Set run current (0-31 scale)
  driver.ihold(0);  // Set hold current (0-31 scale, lower for less power usage when idle)
  driver.iholddelay(10);       // Delay before reducing current to hold current

  // Read back and verify the settings
  Serial.print("Run current (irun): ");
  Serial.println(driver.irun());
// this printed that the UART PORT communication with the TMC2209 is not functioning properly. (or hooked up)

  Serial.print("Hold current (ihold): ");
  Serial.println(driver.ihold());

  Serial.print("Hold delay (iholddelay): ");
  Serial.println(driver.iholddelay());

// Add a UART Communication Test:
  uint32_t gconf_value = driver.GCONF();  // Read GCONF register
Serial.print("GCONF value: ");
Serial.println(gconf_value, BIN);       // Print GCONF value in binary


  update_onboard_led(0);
  // Configure the button pin with an internal pull-up resistor
  pinMode(BTN_BUILTIN, INPUT_PULLUP);  // used for decomissioning

  // Setup Matter and commissioning
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter device is not commissioned");
    Serial.printf("Manual pairing code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
  while (!Matter.isDeviceCommissioned()) {
    delay(200);
  }

  Serial.println("Waiting for Thread network...");
  while (!Matter.isDeviceThreadConnected()) {
    delay(200);
  }
  Serial.println("Connected to Thread network");

  Serial.println("Waiting for Matter device discovery...");
  while (!matter_blinds.is_online()) {
    delay(200);
  }
  Serial.println("Matter device is now online");
}

int32_t last_reported_percent = -1;  // Track the last reported position

void loop() {
  decommission_handler();  // Check if the user button is pressed for 10 seconds

  static uint16_t current_lift_raw = 0u;
  uint16_t requested_lift_raw = matter_blinds.get_requested_lift_position_raw();

  // Ensure current lift position is updated after every movement
  matter_blinds.set_actual_lift_position_raw(current_lift_raw);

  // Get current and requested lift position in percents
  int32_t current_percent = matter_blinds.get_actual_lift_position_percent();
  int32_t requested_percent = matter_blinds.get_requested_lift_position_percent();

  // Return if no movement is requested
  if (current_lift_raw == requested_lift_raw) {
    stop_motor();  // Stop motor function
    return;
  }

  // Determine direction and continuously move until we reach the requested position
  if (current_percent > requested_percent) {
    Serial.println("Rolling down curtains (Closing)...");
    digitalWrite(DIR_PIN, LOW);  // Set direction to roll down (close)
  } else if (current_percent < requested_percent) {
    Serial.println("Rolling up curtains (Opening)...");
    digitalWrite(DIR_PIN, HIGH);  // Set direction to roll up (open)
  }
// debugging
  uint32_t gconf_value = driver.GCONF();  // Read GCONF register
Serial.print("GCONF value: ");
Serial.println(gconf_value, BIN);       // Print GCONF value in binary



  // Move the motor and update the position continuously
  int totalSteps = stepsForFullRoll * movementMultiplier;
  // uint8_t counterLED = 0;
  for (int i = 0; i < totalSteps; i++) {
    step_motor();  // Move motor one step

    // Update the current lift percentage during the motor's movement
    if (current_percent > requested_percent) {
      current_percent--;
    } else {
      current_percent++;
    }

    // Update LED brightness based on the current percent position
    // uint8_t brightness = map(current_percent, 0, 100, 0, 255);


    // if (current_percent == 0 || current_percent == 100) {
    //   counterLED++;
    // }

  // Explicitly handle brightness when current_percent reaches zero
    uint8_t brightness;
    if (current_percent == 0) {
      brightness = 0;  // Ensure brightness stays at 0
    } else {
      // Map the brightness but ignore very low values to avoid flickering
      brightness = map(current_percent, 1, 100, 10, 255);  // Use a minimum brightness of 10 to avoid flickering at low values
    }
    // if (counterLED == 0 || counterLED == 1) {
      update_onboard_led(brightness);
    // }




    // Only report position updates every 10% to avoid excessive updates
    if (abs(current_percent - last_reported_percent) >= 10) {
      last_reported_percent = current_percent;
      matter_blinds.set_actual_lift_position_percent(current_percent);  // Update position in Alexa
    }

    // Delay for smoother movement (adjust based on motor speed)
    delay(50);
  }

  // Once the motor reaches the final position, update the slider and LED one last time
  stop_motor();
  matter_blinds.set_actual_lift_position_raw(requested_lift_raw);     // Set final position
  matter_blinds.set_actual_lift_position_percent(requested_percent);  // Final percentage
  matter_blinds.set_current_operation(MatterWindowCovering::WINDOW_COVERING_STOPPED);
  Serial.printf("Moved to %u%%\n", matter_blinds.get_actual_lift_position_percent());

  current_lift_raw = requested_lift_raw;


}

// Function to move the motor by generating steps
void step_motor() {
    // Enable the TMC2209 motor driver
  digitalWrite(EN_PIN, LOW);  // LOW to enable motor driver

  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(800);  // Reduce pulse width for faster movement
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(800);  // Adjust motor speed
}

// Function to stop the motor
void stop_motor() {
  digitalWrite(STEP_PIN, LOW);  // Stop sending pulses
  digitalWrite(D2, LOW);        // Turn off D2 if used for additional components
  // Serial.println("Motor stopped.");
      // Disable the TMC2209 motor driver
  digitalWrite(EN_PIN, HIGH);  // LOW to disable motor driver
}

// Function to decommission Arduino Nano Matter board
void decommission_handler() {
  // Function to handle decommission logic
  if (digitalRead(BTN_BUILTIN) == LOW) {
    int startTime = millis();
    while (digitalRead(BTN_BUILTIN) == LOW) {
      int elapsedTime = (millis() - startTime) / 1000.0;
      if (elapsedTime > 10) {
        Serial.println("Decommissioning...");
        for (int i = 0; i < 10; i++) {
          digitalWrite(LEDR, !(digitalRead(LEDR)));
          delay(100);
        }
        nvm3_eraseAll(nvm3_defaultHandle);
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("Decommission done!");
        break;
      }
    }
  }
}

// Function used for debugging ThreadNetwork
void printThreadNetworkKey() {
  chip::Thread::OperationalDataset dataset;
  if (chip::DeviceLayer::ThreadStackMgr().GetThreadProvision(dataset) == CHIP_NO_ERROR) {
    uint8_t masterKey[chip::Thread::kSizeMasterKey];
    if (dataset.GetMasterKey(masterKey) == CHIP_NO_ERROR) {
      char keyStr[chip::Thread::kSizeMasterKey * 2 + 1];
      chip::Encoding::BytesToUppercaseHexString(masterKey, chip::Thread::kSizeMasterKey, keyStr, sizeof(keyStr));
      Serial.print("Thread Network Key: ");
      Serial.println(keyStr);
    } else {
      Serial.println("Failed to get Master Key from dataset.");
    }
  } else {
    Serial.println("Failed to retrieve Thread Network Provisioning.");
  }
}

// Update LED brightness continuously during motor movement
void update_onboard_led(uint8_t brightness) {
  static uint8_t previous_brightness = 255;  // Track the previous brightness level

  if (brightness == 0 && previous_brightness != 0) {
    // Only turn off the LED if it's currently on
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("Brightness is zero, turning LED off.");
  } else if (brightness > 0 && brightness != previous_brightness) {
    // Only change the brightness if it’s greater than 0 and different from the previous value
    analogWrite(LED_BUILTIN, brightness);
    Serial.println("Setting brightness: " + String(brightness));
  }

  // Update the previous_brightness so the logic remains consistent
  previous_brightness = brightness;
}