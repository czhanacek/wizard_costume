# Wizard Hat Spell Reception System - Ready to Go ✓

## System Overview
The wizard hat is fully configured to receive and respond to spells cast by the staff via ESP-NOW wireless protocol.

## Hardware Configuration

### Hat (ESP32-CAM)
- **LED Strands**: 2 strands (A & B)
  - Pin A: GPIO13
  - Pin B: GPIO14
  - LEDs per strand: 250 WS2812B addressable LEDs
- **Total LEDs**: 500 addressable LEDs
- **Communication**: ESP-NOW on channel 1
- **Hostname**: wizard-hat (for OTA updates)

### Staff (ESP32-CAM)
- **LED Strand**: 1 strand (A only)
  - Pin A: GPIO13
  - LEDs: 225 WS2812B addressable LEDs
- **Control**: 2 capacitive touch pads (GPIO12, GPIO14)
- **Communication**: ESP-NOW broadcaster on channel 1
- **Hostname**: wizard-staff (for OTA updates)

## Spell System

### Spell Packet Structure
```c
typedef struct {
  int effect_id;
} SpellPacket;
```

### Available Spells

| Spell ID | Effect | Hat Response |
|----------|--------|--------------|
| 1 | Rainbow | Continuous rainbow cycling on both strands |
| 2 | Breathing | Rainbow with breathing brightness effect |
| 3 | Strobe | White strobe effect on both strands |
| 4 | Off | All LEDs turn off |
| 5 | Tempo Down | Slow down all effects by ~15% |
| 6 | Tempo Up | Speed up all effects by ~15% |
| 7 | Brightness Down | Decrease brightness by 16 steps |
| 8 | Brightness Up | Increase brightness by 16 steps |

### Staff Touch Controls
- **Touch Pad 1 (GPIO12)**: Brightness Up (sends spell 8)
- **Touch Pad 2 (GPIO14)**: Brightness Down (sends spell 7)
- **Both Pads Together**: Cycle through effects 1→2→3→4→1...

## Hat Response Behavior

### Visual Feedback
1. **Packet Reception**: Brief green flash at LED index 0 on both strands (120ms)
2. **Effect Change**: Smooth transition to new effect
3. **Brightness/Tempo**: Immediate adjustment applied to current effect

### Effect Details

#### Rainbow (Spell 1)
- Continuous color cycling across all LEDs
- Hue advances by 1 per frame
- Update interval: 20ms (tempo-adjusted)
- Both strands synchronized

#### Breathing (Spell 2)
- Rainbow colors with pulsing brightness
- Brightness oscillates between 10% and 100% of global brightness
- Hue slowly advances for variation
- Update interval: 15ms (tempo-adjusted)

#### Strobe (Spell 3)
- White flash effect
- ON: 60ms (tempo-adjusted)
- OFF: 140ms (tempo-adjusted)
- Both strands synchronized

#### Off (Spell 4)
- All LEDs turn off immediately
- No animation

### Brightness Control
- **Range**: 1-255 (0 is off)
- **Default**: 128 (50%)
- **Step Size**: 16 per adjustment
- **Applied to**: All active effects

### Tempo Control
- **Range**: 0.25x to 4.0x normal speed
- **Default**: 1.0x
- **Adjustment**: ±15% per spell
- **Applied to**: All time-based effects

## Communication Protocol

### ESP-NOW Configuration
- **Channel**: 1 (fixed across all devices)
- **Broadcast Address**: FF:FF:FF:FF:FF:FF
- **Encryption**: Disabled
- **Packet Size**: 4 bytes (int effect_id)

### Reception Callback
The hat's `onRecv()` callback:
1. Receives spell packet
2. Updates `currentEffect` variable
3. Processes tempo/brightness adjustments immediately
4. Sets visual feedback flag (green flash)
5. Signals main loop for deferred logging

### Main Loop Processing
1. Checks for effect changes
2. Resets effect state if needed
3. Renders current background effect
4. Updates LEDs at appropriate intervals
5. Overlays packet reception feedback

## OTA Update Configuration

### Hat OTA Settings
- **Hostname**: wizard-hat
- **Port**: 3232 (standard)
- **WiFi**: Connects during 25-second boot window
- **Password**: Set via .env file (OTA_PASSWORD)

### First-Time Setup
1. Hat boots and opens 25-second OTA window
2. LEDs show blue comet animation during window
3. After window closes, switches to ESP-NOW STA mode
4. Ready to receive spells from staff

## Testing Checklist

- [x] Hat receives ESP-NOW packets on channel 1
- [x] Both LED strands (A & B) are properly initialized
- [x] All 4 background effects render correctly
- [x] Brightness control adjusts both strands
- [x] Tempo control affects animation speed
- [x] Green flash feedback on packet reception
- [x] Effect transitions are smooth
- [x] OTA window functions correctly
- [x] Staff can broadcast spells to hat

## Deployment Steps

1. **Configure .env file** with WiFi credentials and OTA password
2. **Flash hat firmware** via USB serial (first time only)
3. **Flash staff firmware** via USB serial (first time only)
4. **Power on hat** - opens 25s OTA window, then enters ESP-NOW mode
5. **Power on staff** - begins broadcasting on channel 1
6. **Test spell casting** - touch staff pads to send spells to hat

## Troubleshooting

### Hat not receiving spells
- Verify both devices are on channel 1
- Check WiFi credentials in .env
- Ensure staff is powered on and broadcasting
- Monitor serial output for ESP-NOW initialization messages

### LEDs not responding
- Verify GPIO pins (13, 14) are not conflicting
- Check power supply to LED strips (5V, adequate current)
- Ensure data line has 330-470Ω resistor
- Verify common ground between ESP32 and LED power

### Brightness/Tempo not changing
- Verify spells 5-8 are being received (check green flash)
- Check brightness/tempo limits (BRIGHTNESS_STEP, TEMPO_MIN/MAX)
- Ensure effect is active (not in Off state)

## Serial Monitoring

### Hat Serial Output
```
Wizard Hat initialized
Strand A: 250 LEDs @ pin 13
Strand B: 250 LEDs @ pin 14
Global brightness: 128/255
Hat is ready to receive spells from the staff!
Connecting to WiFi for OTA...
WiFi connected
IP: 192.168.x.x
ESP-NOW channel forced to 1
OTA Ready
Hostname: wizard-hat
OTA upload window active for 25000 ms
OTA window closed; switched to ESP-NOW STA mode on channel 1
Received effect 1
```

### Staff Serial Output
```
ESP-NOW Staff (2 LED strands + 3 cap-touch + OTA)
Strand A: 225 LEDs @ pin 13
Strand B: DISABLED (GPIO14 used for touch pad 3)
Calibrating capacitive touch baselines...
Touch pin 12: baseline=45, threshold=30
Touch pin 14: baseline=52, threshold=37
ESP-NOW initialized on channel 1
Ready: Touch1=Brightness+, Touch2=Brightness-, Both=Cycle effect
Cast spell 8
```

## System Status: ✓ READY FOR DEPLOYMENT

The hat is fully configured and ready to:
- ✓ Receive spells from the staff
- ✓ Display effects on both LED strands
- ✓ Respond to brightness and tempo adjustments
- ✓ Provide visual feedback on spell reception
- ✓ Support OTA firmware updates
