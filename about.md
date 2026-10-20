# Input Quantization Assist

An accessibility input buffer for players with high-latency hardware or
physical coordination constraints.

Press **F8** to switch the assist on or off. Press **F9** to cycle the
extra-frame count 1→16; a popup shows the new value and fades after 1 second.
While on,
jump clicks that land within **1-16 physics steps ("extra frames")** of an orb/ring timing window
are corrected:

* **Early clicks** are held until the window opens.
* **Late clicks** are applied to the ring you just passed.

While off, the game runs completely unmodified.

Configure the hotkey, window size and which corrections are active in the mod
settings.
