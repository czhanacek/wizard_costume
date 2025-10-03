# OTA (Over-The-Air) Update Setup for Wizard Costume Receiver

Your receiver code now supports OTA updates! This allows you to update the firmware wirelessly without needing a USB connection.

## Initial Setup

### 1. Configure WiFi Credentials

Edit `src/receiver.cpp` and update these lines with your WiFi credentials:

```cpp
const char* ssid = "YOUR_WIFI_SSID";      // Replace with your WiFi SSID
const char* password = "YOUR_WIFI_PASSWORD";  // Replace with your WiFi password
```

### 2. Optional: Change OTA Password

For security, change the default OTA password in `src/receiver.cpp`:

```cpp
#define OTA_PASSWORD "wizard123"        // Change this to your own password!
```

If you change this, also update it in `platformio.ini`:

```ini
upload_flags = 
    --auth=your_new_password
```

### 3. First Upload via USB

Upload the code to your ESP32 via USB the first time:

```bash
pio run -e receiver -t upload
```

### 4. Monitor Serial Output

After uploading, open the serial monitor to see the device connect to WiFi:

```bash
pio device monitor -e receiver
```

You should see output like:
```
Connecting to WiFi for OTA...
WiFi connected!
IP address: 192.168.1.xxx
OTA Ready
Hostname: wizard-receiver
```

## Using OTA Updates

Once the device is connected to WiFi, you have two options for OTA updates:

### Option 1: Command Line (Recommended)

Upload via OTA using the hostname:

```bash
pio run -e receiver -t upload --upload-port wizard-receiver.local
```

Or using the IP address if mDNS doesn't work:

```bash
pio run -e receiver -t upload --upload-port 192.168.1.xxx
```

### Option 2: Modify platformio.ini

Uncomment the OTA upload_port line in `platformio.ini`:

```ini
; Comment out the serial port:
; upload_port = /dev/cu.usbserial-FTB6SPL3

; Uncomment the OTA port:
upload_port = wizard-receiver.local
```

Then you can upload normally:

```bash
pio run -e receiver -t upload
```

## Features

### Visual Feedback During OTA

- **Blue progress bar**: LEDs show upload progress as a blue bar filling up
- **Red flash**: If an error occurs, all LEDs flash red
- **LEDs off**: During the actual firmware write, LEDs are turned off

### Dual Mode Operation

The receiver operates in `WIFI_AP_STA` mode, which means:
- ✅ ESP-NOW communication still works
- ✅ WiFi connection for OTA updates
- ✅ Both can operate simultaneously

### Fallback Behavior

If WiFi connection fails:
- The device continues with ESP-NOW only
- OTA is disabled but normal operation continues
- You can still upload via USB

## Disabling OTA

If you want to disable OTA (e.g., to save memory or for production), edit `src/receiver.cpp`:

```cpp
#define OTA_ENABLED 0  // Change from 1 to 0
```

This will remove all OTA code and revert to ESP-NOW only mode.

## Troubleshooting

### Can't find device at wizard-receiver.local

1. Try using the IP address instead
2. Check that your computer and ESP32 are on the same network
3. Some networks block mDNS - try a different network

### OTA upload fails with "Auth Failed"

- Check that the password in `platformio.ini` matches the password in `src/receiver.cpp`

### WiFi won't connect

1. Double-check your SSID and password
2. Make sure your WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
3. Check serial monitor for connection status

### ESP-NOW stops working after enabling OTA

- This shouldn't happen as both modes are compatible
- If it does, check that WiFi channel matches between sender and receiver
- Try disabling OTA temporarily to isolate the issue

## Security Notes

- **Change the default password!** The default `wizard123` is not secure
- OTA updates are only available on your local network
- Consider disabling OTA for production/public deployments
