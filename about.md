# Input Quantization Assist

An accessibility input buffer for players with high-latency hardware or
physical coordination constraints.

Press the hotkey (default **F8**) to switch the assist on or off. While on,
jump clicks that land within **1-2 physics steps** of an orb/ring timing window
are corrected:

* **Early clicks** are held until the window opens.
* **Late clicks** are applied to the ring you just passed.

While off, the game runs completely unmodified.

Configure the hotkey, window size and which corrections are active in the mod
settings.
