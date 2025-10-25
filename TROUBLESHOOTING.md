# ESP-NOW Troubleshooting Guide

## Current Issue
Staff and cape are not communicating via ESP-NOW.

## Diagnostic Steps

### 1. Verify Both Devices Boot Properly
**Staff Serial Output Should Show:**
```
ESP-NOW Staff (2 LED strands + 3 cap-touch + OTA)
Strand A: 225 LEDs @ pin 13
ESP-NOW initialized on channel 1
```

**Cape Serial Output Should Show:**
```
WS2812B LED Strip Cape (with NetSerial)
Controlling 50 LEDs per strip across 4 strips
Stole strand: 250 LEDs on pin 12
ESP-NOW reinitialized on channel 1
Current channel after switch: 1
```

### 2. Test Staff Broadcasting
Press Button 1 on staff - you should see:
```
Cast spell 1
Effect: Rainbow
```

### 3. Test Cape Receiving
When staff sends spell, cape should show:
```
Received effect 1
```

## Common Issues

### Issue: Cape shows different channel than 1
**Solution**: The cape's WiFi connection might be changing the channel. Verify:
- Cape serial shows "Current channel after switch: 1"
- If not, the SoftAP isn't pinning the channel correctly

### Issue: Staff shows "Cast spell X" but cape receives nothing
**Possible causes:**
1. **Timing**: Cape might still be in OTA window (wait 25 seconds after boot)
2. **Channel mismatch**: Verify both show channel 1
3. **ESP-NOW not initialized**: Cape should show "ESP-NOW reinitialized"

### Issue: Neither device shows channel information
**Solution**: Power cycle both devices and check serial output immediately

## Quick Test Without WiFi

### Disable OTA on Cape
Edit `src/cape.cpp`:
```cpp
#define OTA_ENABLED 0  // Change from 1 to 0
```

This will:
- Skip WiFi connection attempt
- Go straight to ESP-NOW mode
- Eliminate any WiFi-related channel issues

### Verify Channel Pinning
Both devices should create a SoftAP:
- **Staff**: `WiFi.softAP("wr-sync", "", 1, 1)`
- **Cape**: `WiFi.softAP("cape-sync", "", 1, 1)`

## Expected Behavior

### Staff (Sender)
1. Boots up
2. Creates hidden SoftAP on channel 1
3. Initializes ESP-NOW
4. Waits for button presses
5. Broadcasts spells when buttons pressed

### Cape (Receiver)
1. Boots up
2. Tries WiFi (fails if no AP)
3. Creates hidden SoftAP on channel 1
4. Initializes ESP-NOW with receive callback
5. Waits for incoming spells
6. Processes spells and updates LEDs

## Next Steps
1. Power cycle both devices
2. Wait 30 seconds (past OTA windows)
3. Check serial output from both
4. Press staff Button 1
5. Verify cape shows "Received effect 1"
