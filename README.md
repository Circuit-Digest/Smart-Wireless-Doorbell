# How to Build a Smart Wireless Doorbell System using IoT

Missing a visitor at the front door usually means missed package deliveries, keeping friends waiting outside, or missing unexpected guests. Standard doorbells only work if we happen to be sitting nearby and the chime is loud enough to hear. If we are stuck in a work meeting, listening to music with headphones, or away from home, it is easy to miss the ring, leaving visitors waiting outside the house with no idea if anyone is actually home.

To bridge this gap, we made a smart and connected solution that brings real-time presence monitors and instant interaction to the person in front of the door. Whether we are in another room or not in the house, this system ensures we never miss a visitor at our front door.

---

## 📌 How Does This Smart ESP32 Voice Doorbell Work?

When a visitor presses the front door button, the **ESP32-CAM** detects the input through an interrupt on **GPIO 13** and immediately begins executing its notification sequence:
1. **Audio Voice Prompt**: It plays a pre-recorded audio prompt (*"Doorbell ringing, please wait"*) from flash memory to the **MAX98357A I2S amplifier**, driving the 8Ω outdoor speaker via Direct Memory Access (DMA).
2. **Camera Sensor Initialization**: Simultaneously, the microcontroller initializes the **OV2640 camera sensor**, applies exposure and white balance adjustments, and clears stalled frame buffers.
3. **JPEG Capture & Buzzer Scheduling**: It captures a JPEG image of the visitor and schedules a non-blocking timer to activate the indoor active buzzer on **GPIO 12** for 3 seconds.
4. **Cloud & WhatsApp Transmission**: Once captured, the ESP32-CAM establishes a secure TLS connection over Wi-Fi to push the image directly to the **CircuitDigest Cloud** dashboard. Simultaneously, it constructs an HTTPS POST request to the cloud messaging API to dispatch an instant photo alert directly to the user's **WhatsApp**.
5. **Two-Way Voice Response Controls**: When the user selects a quick response toggle on the cloud dashboard, an MQTT message is published to the ESP32-CAM. The microcontroller parses this payload in its main execution loop and streams the corresponding WAV file (e.g., delivery instructions or arrival delays) to the outdoor speaker.

### 🎙️ Spoken Audio Responses Table

| Scenario / Dashboard Action | Spoken Audio Line |
| :--- | :--- |
| **After Button Pressed** | *"Doorbell ringing, please wait"* |
| **Deliveries & Couriers Button** | *"Please leave the package by the door. Thank you!"* |
| **Working, Busy, or Resting Button** | *"We can't come to the door right now. Please check back later."* |
| **Buying Time to Get to Door Button** | *"Please wait! We'll be right there."* |
| **While Away (Friends/Family/Neighbors)** | *"We're not home right now. Please give us a call!"* |

---

## 🛠️ Components Required

| S.No | Component | Specification | Quantity |
| :--- | :--- | :--- | :--- |
| 1 | Microcontroller | ESP32-CAM | 1 |
| 2 | Audio Amplifier | MAX98357A I2S | 1 |
| 3 | Push Button | Momentary Push Button | 1 |
| 4 | Speaker | 8Ω Outdoor Speaker | 1 |
| 5 | Buzzer | Active Buzzer Module | 1 |
| 6 | Charging Module | TP4056 | 1 |
| 7 | Capacitor | Electrolytic (100uF) | 2 |

---

## 🎵 .MP4 to C-Byte Array Conversion

To store the voice prompts directly in the ESP32’s flash memory without using an external SD card:
1. Export original audio/video clips as mono `.wav` audio files using Audacity (formatted to **16 kHz sample rate in 16-bit PCM**).
2. Convert the `.wav` files into raw C-byte arrays using the web-based **FileToCArray** tool.
3. Place the C-arrays inside [`Smart_Wireless_Doorbell/audio_data.h`](Smart_Wireless_Doorbell/audio_data.h).

The ESP32 fetches and streams the raw PCM buffers (`voice_doorbell`, `voice_switch1` to `voice_switch4`) directly to the MAX98357A I2S amplifier when triggered.

---

## ⚡ Circuit Diagram & Hardware Wiring

### System Power & Push Button Wiring
- **TP4056 Charging Module**: Connect `IN+` terminal to **5V** of ESP32-CAM and `IN-` to **GND**.
- **Doorbell Push Button**: Connect one terminal of the Momentary push button to **GPIO 13** of ESP32-CAM and the other terminal to **GND**.
- **Decoupling Capacitors**: Place one `100uF` electrolytic capacitor between **5V** and **GND**, and another `100uF` electrolytic capacitor between **3V3** and **GND** of the ESP32-CAM.

### Active Buzzer Wiring

| Buzzer Module Pin | ESP32-CAM Pin |
| :--- | :--- |
| **VCC** | **3V3** |
| **I/O** | **GPIO 12** |
| **GND** | **GND** |

### MAX98357A Audio Amplifier Wiring

| MAX98357A Amplifier Pin | ESP32-CAM Pin / Terminal |
| :--- | :--- |
| **LRCLK** | **GPIO 14** |
| **BCLK** | **GPIO 2** |
| **DIN** | **GPIO 15** |
| **VO+** | **Speaker Positive Terminal** |
| **VO-** | **Speaker Negative Terminal** |

---

## 🌐 CircuitDigest Cloud Setup Guide

1. **Sign Up / Login**: Log into your account on [CircuitDigest Cloud](https://www.circuitdigest.cloud).
2. **Adding New Device**: Navigate to **Dashboard** $\rightarrow$ **Devices** $\rightarrow$ **Add New Device**. Set device name as `Smart Door Bell` and click **Add Device**.
3. **Adding New Variables**: Under **Variables**, select `Smart Door Bell` from the dropdown and create 4 variables:
   - `deliveries` $\rightarrow$ Key: `analog-input-1` (Bidirectional)
   - `busy` $\rightarrow$ Key: `analog-input-2` (Bidirectional)
   - `buying time` $\rightarrow$ Key: `analog-input-3` (Bidirectional)
   - `friends or neighbors` $\rightarrow$ Key: `analog-input-4` (Bidirectional)
4. **Adding Widgets**: Go to **Dashboard**, click **Add Widget** $\rightarrow$ **Toggle Switch Widget**. Add 4 toggle switches mapping to `analog-input-1` through `analog-input-4`.
5. **WhatsApp API Integration**: Go to the **Home** tab $\rightarrow$ **WhatsApp Notification** $\rightarrow$ **Link Number**. Enter your mobile number, verify the OTP, and copy your `API_KEY`.

---

## 💻 Code Structure & Flashing

The firmware is located under [`Smart_Wireless_Doorbell/Smart_Wireless_Doorbell.ino`](Smart_Wireless_Doorbell/Smart_Wireless_Doorbell.ino) and [`Smart_Wireless_Doorbell/audio_data.h`](Smart_Wireless_Doorbell/audio_data.h).

### Dependencies
Install the required libraries in Arduino IDE:
- `CircuitDigestCloud`
- `WiFi`
- `WiFiClientSecure`
- `esp_camera`

### Wi-Fi & Credentials Configuration
Update the following definitions inside `outdoor-unit.ino`:
```cpp
#define WIFI_SSID             "your-Wi-Fi-name"
#define WIFI_PASS             "Your-Wi-Fi-password"
#define DEVICE_ID             "Your-Device-ID"
#define CONNECTION_KEY        "Your-Connection-Key"
#define API_KEY               "Your-API-Key"
#define WHATSAPP_PHONE_NUMBER "yourphone number" // e.g. 91xxxxxxxxxx
```

---

## 🔧 Troubleshooting & FAQs

1. **Which ohm rating of speaker to use?**
   - You can use 4-Ω, 6-Ω, or 8-Ω speakers. **Never use a speaker rated below 4 ohms**, as it can overheat or damage the amplifier.
2. **Why is the sound distorted, crackling, or cutting out during playback?**
   - Ensure the MAX98357A amplifier is powered from a stable 5V rail with adequate current capacity.
3. **Why does the ESP32-CAM reset when voice audio starts playing?**
   - The amplifier and Wi-Fi radio combined exceed the current limit of a standard computer USB port. Power the system using a dedicated **5V / 2A DC power adapter**.
4. **What should I do if the speaker emits a high-pitched buzzing or humming noise when idle?**
   - Ensure a single, common ground (**GND**) point connection between the ESP32-CAM, MAX98357A, and buzzer.

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
