# Staff 2-Button Spell UI Guide

## Overview
The staff now features an intuitive 2-button interface for casting spells to the hat. The UI uses **mode switching** to organize spell casting into three logical categories.

## Hardware
- **Button 1 (Pad 0)**: GPIO12 - Left touch pad
- **Button 2 (Pad 1)**: GPIO14 - Right touch pad
- **Both Buttons**: Hold simultaneously for 3+ seconds to reset

## UI Modes

### Mode 1: EFFECTS (Default)
**Purpose**: Cycle through visual effects on the hat

**Button 1 Action**: Cycle to next effect
- Rainbow → Breathing → Strobe → Off → Rainbow...

**Button 2 Action**: Switch to BRIGHTNESS mode

**LED Indicator**: LED 1 shows rainbow color (cycling)

**Serial Output**:
```
Effect: Rainbow
Effect: Breathing
Effect: Strobe
Effect: Off
```

---

### Mode 2: BRIGHTNESS
**Purpose**: Adjust the brightness of the hat's LEDs

**Button 1 Action**: Increase brightness (16 steps)
- Range: 1-255
- Default: 128 (50%)

**Button 2 Action**: Switch to TEMPO mode

**LED Indicator**: LED 1 shows solid RED

**Serial Output**:
```
Mode: BRIGHTNESS (adjust with Button 1)
Brightness: 144/255
Brightness: 160/255
```

---

### Mode 3: TEMPO
**Purpose**: Adjust animation speed on the hat

**Button 1 Action**: Increase tempo (×1.15 per press)
- Range: 0.25x to 4.0x normal speed
- Default: 1.0x

**Button 2 Action**: Switch back to EFFECTS mode

**LED Indicator**: LED 1 shows solid BLUE

**Serial Output**:
```
Mode: TEMPO (adjust with Button 1)
Tempo: 1.15x
Tempo: 1.32x
```

---

## Special Actions

### Reset to Default
**Action**: Hold both buttons simultaneously for 3+ seconds

**Effect**:
- Switches to EFFECTS mode
- Sets effect to Rainbow (spell 1)
- Resets brightness to 128/255
- Resets tempo to 1.0x
- Sends reset spell to hat

**Serial Output**:
```
RESET: Effects mode, brightness 128, tempo 1.0x
```

---

## Auto-Timeout
If you stay in BRIGHTNESS or TEMPO mode for **3 seconds without pressing any button**, the staff automatically returns to EFFECTS mode.

**Serial Output**:
```
Mode timeout: Back to EFFECTS
```

---

## Visual Feedback

### LED Indicators (on staff)
- **LED 0**: Green flash = Spell transmitted (120ms pulse)
- **LED 1**: Mode indicator
  - Rainbow (cycling) = EFFECTS mode
  - Red (solid) = BRIGHTNESS mode
  - Blue (solid) = TEMPO mode

### Hat Response
- **Green flash at LED 0**: Spell received
- **Effect change**: Smooth transition to new effect
- **Brightness/Tempo**: Immediate adjustment to current effect

---

## Usage Examples

### Example 1: Change Effect
```
1. Press Button 1 → Rainbow effect
2. Press Button 1 → Breathing effect
3. Press Button 1 → Strobe effect
4. Press Button 1 → Off
5. Press Button 1 → Back to Rainbow
```

### Example 2: Adjust Brightness
```
1. Press Button 2 → Switch to BRIGHTNESS mode (LED 1 turns red)
2. Press Button 1 → Brightness increases
3. Press Button 1 → Brightness increases more
4. Wait 3 seconds → Auto-return to EFFECTS mode
```

### Example 3: Speed Up Animation
```
1. Press Button 2 → BRIGHTNESS mode (LED 1 red)
2. Press Button 2 → TEMPO mode (LED 1 blue)
3. Press Button 1 → Animation speeds up
4. Press Button 1 → Animation speeds up more
5. Press Button 2 → Back to EFFECTS mode
```

### Example 4: Full Reset
```
1. Hold both buttons for 3+ seconds
2. Staff resets to: Rainbow effect, brightness 128, tempo 1.0x
3. Hat receives reset spell and displays rainbow
```

---

## Serial Monitoring

### Boot Output
```
=== 2-BUTTON SPELL UI ===
Button 1 (Pad 0): Cycle Effects (Rainbow -> Breathing -> Strobe -> Off)
Button 2 (Pad 1): Cycle Modifiers (Brightness -> Tempo -> back to Effects)
Hold Both: Reset to Default
Current Mode: EFFECTS
========================
```

### Real-Time Feedback
```
Effect: Rainbow
Cast spell 1
Effect: Breathing
Cast spell 2
Mode: BRIGHTNESS (adjust with Button 1)
Brightness: 144/255
Cast spell 8
Mode: TEMPO (adjust with Button 1)
Tempo: 1.15x
Cast spell 6
RESET: Effects mode, brightness 128, tempo 1.0x
Cast spell 1
Mode timeout: Back to EFFECTS
```

---

## Spell Mapping

| Spell ID | Triggered By | Effect |
|----------|--------------|--------|
| 1 | Button 1 in EFFECTS | Rainbow effect |
| 2 | Button 1 in EFFECTS | Breathing effect |
| 3 | Button 1 in EFFECTS | Strobe effect |
| 4 | Button 1 in EFFECTS | Off |
| 6 | Button 1 in TEMPO | Increase tempo |
| 8 | Button 1 in BRIGHTNESS | Increase brightness |

---

## Troubleshooting

### Touch Pads Not Responding
- Check serial output for touch calibration values
- Ensure fingers are clean and dry
- Try pressing with more pressure
- Check touch pad baseline values in serial output

### Mode Not Switching
- Verify Button 2 is being pressed (should see "Mode:" message)
- Check that you're not holding both buttons (which triggers reset)
- Wait for auto-timeout if stuck in a mode

### Spells Not Reaching Hat
- Verify green flash on staff LED 0 (transmission confirmation)
- Check hat serial output for "Received effect X"
- Ensure both devices are on same ESP-NOW channel (1)
- Verify hat is powered on and in ESP-NOW mode (not OTA window)

### Brightness/Tempo Not Changing
- Verify you're in the correct mode (red for brightness, blue for tempo)
- Check that effect is active (not in Off state)
- Verify spells 6 and 8 are being received on hat

---

## Advanced: Serial Console Commands

You can also send spells via serial console (0-9):
- `1` = Rainbow effect
- `2` = Breathing effect
- `3` = Strobe effect
- `4` = Off
- `5` = Tempo down
- `6` = Tempo up
- `7` = Brightness down
- `8` = Brightness up

Example: Type `1` in serial monitor to send rainbow effect

---

## System Status: ✓ READY

The staff is fully configured with an intuitive 2-button UI that:
- ✓ Cycles through 4 visual effects
- ✓ Adjusts brightness in 16-step increments
- ✓ Adjusts animation tempo from 0.25x to 4.0x
- ✓ Provides visual feedback via LED indicators
- ✓ Auto-returns to effects mode after 3 seconds
- ✓ Supports full reset with both buttons
- ✓ Sends spells reliably to the hat via ESP-NOW
