# Staff 2-Button Spell UI Guide

## Overview
The staff features an intuitive 2-button interface with **hold/tap combo detection** for casting spells. Single taps trigger basic actions, while holding one button and tapping the other enables advanced brightness control.

## Hardware
- **Top Button (Pad 0)**: GPIO12 - Top touch pad
- **Bottom Button (Pad 1)**: GPIO14 - Bottom touch pad
- **Hold Threshold**: 300ms to register as "hold"
- **Both-Hold Threshold**: 400ms for both-hold action

## Control Methods

### Single Tap Actions
**Tap Top Button**: Cycle Effects
- Rainbow → Breathing → Off → Rainbow...
- Sends spell 1, 2, or 3

**Tap Bottom Button**: Tempo Up
- Increases animation speed by ×1.2x
- Range: 0.25x to 4.0x normal speed
- Sends spell 10

### Combo Actions (Hold + Tap)

**Hold Top + Tap Bottom**: Brightness Down
- Decreases brightness by 16/255 steps
- Range: 1-255, Default: 128 (50%)
- Sends spell 7

**Hold Bottom + Tap Top**: Brightness Up
- Increases brightness by 16/255 steps
- Range: 1-255, Default: 128 (50%)
- Sends spell 8

**Hold Both > 0.4s**: Shoot Animation
- Triggers one-shot "shoot" animation down the staff
- Sends spell 12
- Note: Animation implementation depends on receiver

### Disabled Actions
**Tap Both**: No action (disabled)
- Prevents accidental triggers

---

## Visual Feedback

### LED Indicators (on staff)
- **LED 0**: Green flash = Spell transmitted (120ms pulse)
- **Background effect**: Shows current effect
  - Rainbow (cycling) = Rainbow effect active
  - Breathing (pulsing) = Breathing effect active
  - Off = No effect

### Hat/Receiver Response
- **Green flash at LED 0**: Spell received
- **Effect change**: Smooth transition to new effect
- **Tempo change**: Immediate adjustment to animation speed
- **Brightness change**: Immediate adjustment to LED brightness

---

## Usage Examples

### Example 1: Change Effect
```
1. Tap Top Button → Rainbow effect
2. Tap Top Button → Breathing effect
3. Tap Top Button → Off
4. Tap Top Button → Back to Rainbow
```

### Example 2: Speed Up Animation
```
1. Tap Bottom Button → Animation speeds up (×1.2x)
2. Tap Bottom Button → Animation speeds up more (×1.44x)
3. Tap Bottom Button → Animation speeds up more (×1.73x)
```

### Example 3: Adjust Brightness Down
```
1. Hold Top Button (300ms+) then tap Bottom Button → Brightness decreases by 16/255
2. Hold Top again and tap Bottom → Brightness decreases more
3. Hold Top again and tap Bottom → Brightness decreases more
```

### Example 4: Adjust Brightness Up
```
1. Hold Bottom Button (300ms+) then tap Top Button → Brightness increases by 16/255
2. Hold Bottom again and tap Top → Brightness increases more
3. Hold Bottom again and tap Top → Brightness increases more
```

### Example 5: Trigger Shoot Animation
```
1. Hold both Top and Bottom buttons for 400ms+ → Shoot animation fires
2. Lights shoot down the staff from top to bottom
```

---

## Serial Monitoring

### Boot Output
```
=== SIMPLE 2-BUTTON SPELL UI ===
Top Button: Cycle Effects (Rainbow -> Breathing -> Off)
Bottom Button: Tempo Up
Hold Top + Tap Bottom: Brightness Down
Hold Bottom + Tap Top: Brightness Up
Hold Both > 0.4s: Shoot Animation
==================================
```

### Real-Time Feedback
```
Pad 0 pressed at 1234567 ms
TAP: Top Button -> Cycle Effect
Effect: Rainbow
Cast spell 1

Pad 1 pressed at 1234890 ms
TAP: Bottom Button -> Tempo Up
Tempo: 1.20x
Cast spell 10

Pad 0 pressed at 1235100 ms
Pad 0 held (> 300 ms)
Pad 1 pressed at 1235400 ms
COMBO: Hold Top + Tap Bottom -> Brightness Down
Brightness: 112/255
Cast spell 7

Both pads pressed together at 1235600 ms
COMBO: Both held > 0.4s -> Shoot Animation
Cast spell 12
```

---

## Spell Mapping

| Spell ID | Triggered By | Effect |
|----------|--------------|--------|
| 1 | Tap Top Button | Rainbow effect |
| 2 | Tap Top Button | Breathing effect |
| 3 | Tap Top Button | Off |
| 7 | Hold Top + Tap Bottom | Decrease brightness (-16/255) |
| 8 | Hold Bottom + Tap Top | Increase brightness (+16/255) |
| 10 | Tap Bottom Button | Increase tempo (×1.2x) |
| 12 | Hold Both > 0.4s | Shoot animation (one-shot) |

---

## Troubleshooting

### Touch Pads Not Responding
- Check serial output for touch calibration values
- Ensure fingers are clean and dry
- Try pressing with more pressure
- Check touch pad baseline values in serial output

### Combos Not Triggering
- Verify hold duration: must hold for 300ms+ to register as "hold"
- For both-hold: must hold both pads together for 400ms+
- Check serial output for "Pad X held" and "COMBO:" messages
- Ensure you're not releasing the held pad before tapping the other

### Spells Not Reaching Hat
- Verify green flash on staff LED 0 (transmission confirmation)
- Check hat serial output for "Received effect X"
- Ensure both devices are on same ESP-NOW channel (1)
- Verify hat is powered on and in ESP-NOW mode (not OTA window)

### Tempo Not Changing
- Verify you're tapping the bottom button (not using combos)
- Check that effect is active (not in Off state)
- Verify spell 10 is being received on hat
- Check tempo range: 0.25x to 4.0x (may be at limits)

### Brightness Not Changing
- Verify you're using the combo controls (Hold + Tap)
- Check that effect is active (not in Off state)
- Verify spells 7 or 8 are being received on hat
- Check brightness range: 1-255 (may be at limits)

---

## Advanced: Serial Console Commands

You can also send spells via serial console (0-9):
- `1` = Rainbow effect
- `2` = Breathing effect
- `3` = Off
- `7` = Brightness down
- `8` = Brightness up
- `10` = Tempo up (type `1` then `0`)
- `12` = Shoot animation (type `1` then `2`)

Example: Type `1` in serial monitor to send rainbow effect

---

## System Status: ✓ READY

The staff is fully configured with an intuitive 2-button UI that:
- ✓ Cycles through 3 visual effects (tap Top)
- ✓ Adjusts tempo with single tap (tap Bottom)
- ✓ Adjusts brightness via hold+tap combos
- ✓ Triggers one-shot shoot animation (hold both)
- ✓ Provides visual feedback via LED indicators
- ✓ Supports hold/tap combo detection (300ms hold threshold)
- ✓ Supports both-hold action (400ms threshold)
- ✓ Sends spells reliably via ESP-NOW

---

## Notes on One-Shot Animations

The "shoot animation" (spell 12) is designed to create a visual effect where lights appear to shoot down the staff from top to bottom. The actual animation implementation depends on the receiver (hat/cape) code. Consider implementing:

- **Fast comet**: A bright pixel that travels down the LEDs quickly
- **Color wave**: A wave of color that cascades down
- **Sparkle trail**: Sparkles that appear sequentially from top to bottom
- **Energy pulse**: A pulsing energy effect that moves downward

The staff sends the spell command; the receiver handles the animation rendering.
