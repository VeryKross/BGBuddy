# BGBuddy
BG Buddy is a blood glucose monitoring project that provides a local IoT-based display for Nightscout users.

BG Buddy leverages the ESP8266 SOC, an inexpensive OLED display, and a few other optional components. Since BG Buddy depends on a user's Nightscout website for data, this has to be setup and operational before anything wonderful can come from BG Buddy. If you're not familiar with Nightscout, please refer to this link for all the details and come back here once you're setup: http://www.nightscout.info/

V1 - BG Buddy simply monitors the Nightscout API and displays 5 key pieces of information which, for me, would cause me to check on a loved one with diabetes if any of the values were out of range. That's really what's at the core of BG Buddy - keep those key values top-of-mind by creating a simple inexpensive device that could sit on a desk or shelf [or both] so these critical values are always available, at a glance.

![image](https://user-images.githubusercontent.com/11561147/206937646-eebd5c93-2601-4517-baed-061cf33e25ca.png)

![BG_Buddy1_Breadboard1](https://user-images.githubusercontent.com/11561147/208538830-5ceb35ea-0ee1-4cdb-b980-db8bcfabd936.jpg)

V2 - Version 2 of BG Buddy is already in the works and will add visual and audible alarm options that will be triggered by the settings configured in Nightscout itself. The user will have new options to control whether they want just the warning LED, the buzzer, or both (if you don't want either, stick with V1). More details on the build will follow soon.

V3 - Version 3 again builds on its predecessor (v2) and adds the ability to manually trigger speech. Yes, BG Buddy will be able to talk, calling out the values for those 5 tracked values and making it accessible to those with visual impairments. This is in the early stages of development, and I’m excited to include this functionality.

Lastly, full build instructions, BOM, and probably a video will be coming along. Source and compiled bin options will be available so if you really don't want to get into compiling stuff and just want a working BG Buddy, that will be an option too (you'll still need to hook the components together, but its pretty easy, I promise).

BG Buddy is a DIY project intended for educational and personal use only. It is not intended for commercial use or to be relied upon for any professional or medical purposes. The creators and contributors of BG Buddy are not responsible for any damages or injuries that may result from the use of this project. By using the BG Buddy software and/or hardware plans, you acknowledge that you understand and assume all risks associated with its use. Please use caution and common sense when working on any DIY project.
