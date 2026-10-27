import 'settings.dart';

/// Minimal Hindi / English text table. Pick the language in Settings.
class Tr {
  const Tr(this.hi);

  factory Tr.of(AppSettings s) => Tr(s.hindi);

  final bool hi;

  String _t(String h, String e) => hi ? h : e;

  String get settings => _t('सेटिंग्स', 'Settings');
  String get refresh => _t('रीफ़्रेश', 'Refresh');
  String get switchDevice => _t('डिवाइस बदलें', 'Switch device');
  String get retry => _t('फिर कोशिश करें', 'Try again');
  String get openSettings => _t('सेटिंग्स खोलें', 'Open settings');
  String get cantReach => _t('ESP32 से कनेक्ट नहीं हुआ', "Couldn't reach the ESP32");
  String get cantReachHelp => _t(
      'जाँचें कि फ़ोन और ESP32 एक ही Wi-Fi या VPN पर हैं और ESP32 चालू है।',
      'Check that your phone and the ESP32 are on the same Wi-Fi or VPN and that the ESP32 is powered on.');
  String get timedOut => _t('पेज समय पर नहीं खुला', 'The page took too long to load');
  String get settingsHint => _t('बदलाव अपने-आप सेव हो जाते हैं।', 'Changes are saved automatically.');

  String get sectionGeneral => _t('सामान्य', 'General');
  String get sectionDevices => _t('डिवाइस', 'Devices');
  String get sectionAppearance => _t('रूप-रंग', 'Appearance');
  String get sectionBehaviour => _t('व्यवहार', 'Behaviour');
  String get sectionLanguage => _t('भाषा', 'Language');
  String get sectionAbout => _t('सुरक्षा और जानकारी', 'Security and info');

  String get appName => _t('ऐप का नाम', 'App name');
  String get addDevice => _t('डिवाइस जोड़ें', 'Add device');
  String get editDevice => _t('डिवाइस बदलें', 'Edit device');
  String get deviceName => _t('डिवाइस का नाम', 'Device name');
  String get deviceUrl => _t('ESP32 URL या IP', 'ESP32 URL or IP');
  String get deviceUrlHint => 'http://192.168.1.37/';
  String get invalidUrl => _t('सही IP या http/https URL डालें।', 'Enter a valid IP or http/https URL.');
  String get nameRequired => _t('नाम खाली नहीं हो सकता।', 'Name can’t be empty.');
  String get delete => _t('हटाएँ', 'Delete');
  String get cancel => _t('रद्द करें', 'Cancel');
  String get save => _t('सेव करें', 'Save');
  String get ok => _t('ठीक है', 'OK');

  String get themeMode => _t('थीम', 'Theme');
  String get themeSystem => _t('सिस्टम', 'System');
  String get themeLight => _t('लाइट', 'Light');
  String get themeDark => _t('डार्क', 'Dark');
  String get accent => _t('मुख्य रंग', 'Accent colour');
  String get customColour => _t('अपना रंग (HEX)', 'Custom colour (HEX)');
  String get invalidHex => _t('6 अंकों का HEX कोड डालें, जैसे 0F766E।', 'Enter a 6-digit HEX code, e.g. 0F766E.');
  String get showToolbar => _t('ऊपर की टूलबार दिखाएँ', 'Show top toolbar');
  String get showToolbarHelp => _t('बंद करने पर पूरी स्क्रीन पर पेज दिखेगा।', 'Turn off for a full-screen page.');
  String get pageZoom => _t('पेज ज़ूम', 'Page zoom');
  String get autoRefresh => _t('ऑटो-रीफ़्रेश', 'Auto-refresh');
  String get off => _t('बंद', 'Off');
  String every(int seconds) {
    if (seconds >= 60) {
      final m = seconds ~/ 60;
      return _t('हर $m मिनट', 'Every $m min');
    }
    return _t('हर $seconds सेकंड', 'Every $seconds s');
  }

  String get loadTimeout => _t('लोड होने का समय-सीमा', 'Load timeout');
  String seconds(int s) => _t('$s सेकंड', '$s s');
  String get javaScript => _t('JavaScript चालू रखें', 'Enable JavaScript');
  String get javaScriptHelp => _t(
      'ESP32 के बटन और सेंसर के लिए ज़रूरी है।', 'Needed for the ESP32 buttons and sensor data.');
  String get language => _t('ऐप की भाषा', 'App language');
  String get resetAll => _t('सभी सेटिंग्स रीसेट करें', 'Reset all settings');
  String get resetConfirm => _t('सभी सेटिंग्स डिफ़ॉल्ट पर लौट जाएँगी।', 'All settings will return to their defaults.');
  String get securityNote => _t(
      'घर के बाहर से कंट्रोल के लिए क्लाउड (MQTT) कनेक्शन इस्तेमाल करें: वह एन्क्रिप्टेड है और सर्टिफ़िकेट जाँचता है। लोकल कनेक्शन आपके Wi-Fi पर सादा HTTP है, इसलिए बोर्ड का पोर्ट 80 कभी इंटरनेट पर न खोलें।',
      'Away from home, use the Cloud (MQTT) connection: it is encrypted and certificate-checked. The local connection is plain HTTP on your Wi-Fi, so never expose a board\'s port 80 to the internet.');

  // ---------------------------------------------------------------- dashboard
  static const _wdHi = ['सोम', 'मंगल', 'बुध', 'गुरु', 'शुक्र', 'शनि', 'रवि'];
  static const _wdEn = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  static const _moHi = ['जन', 'फ़र', 'मार्च', 'अप्रै', 'मई', 'जून', 'जुल', 'अग', 'सित', 'अक्टू', 'नव', 'दिस'];
  static const _moEn = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

  /// weekday: DateTime.weekday (1 = Monday ... 7 = Sunday)
  String weekday(int weekday) => (hi ? _wdHi : _wdEn)[(weekday - 1).clamp(0, 6).toInt()];

  /// month: DateTime.month (1..12)
  String month(int month) => (hi ? _moHi : _moEn)[(month - 1).clamp(0, 11).toInt()];

  String get connectedTo => _t('कनेक्टेड: ', 'Connected to: ');
  String get status => _t('स्थिति: ', 'Status: ');
  String get online => _t('ऑनलाइन', 'Online');
  String get offline => _t('ऑफ़लाइन', 'Offline');
  String get systemState => _t('ESP32 सिस्टम स्थिति', 'ESP32 System State');
  String get connecting => _t('ESP32 से जुड़ रहे हैं…', 'Connecting to ESP32…');
  String get openWebPage => _t('ESP32 वेब पेज', 'ESP32 web page');

  // errors
  String get errNetwork => _t('ESP32 से कनेक्ट नहीं हो पाया। Wi-Fi और IP जाँचें।', "Can't reach the ESP32. Check Wi-Fi and the IP address.");
  String get errAuth => _t('इस बोर्ड का access token नहीं मिला या गलत है। Settings → Boards में बोर्ड दोबारा जोड़ें।', "This board's access token is missing or was rejected. Add the board again in Settings → Boards.");
  String get errFirmware => _t(
      'ESP32 मिल गया, लेकिन उसमें नया फ़र्मवेयर नहीं है। esp32_smart_home.ino फ़्लैश करें (या ऊपर 🌐 से पुराना वेब पेज खोलें)।',
      'The ESP32 answered, but it is not running the new firmware. Flash esp32_smart_home.ino (or open the old web page with the globe icon).');
  String get errServer => _t('ESP32 ने गलती लौटाई।', 'The ESP32 returned an error.');
  String get notConnected => _t('ESP32 कनेक्टेड नहीं है।', 'ESP32 is not connected.');

  // voice
  String get voiceAssistant => _t('वॉइस असिस्टेंट', 'Voice Assistant');
  String get voicePrompt => _t('बोलिए, जैसे "लाइट चालू करो"', 'Say something like "light on"');
  String voiceHeard(String text) => _t('सुना: “$text”', 'Heard: “$text”');
  String get voiceNoMatch => _t('समझ नहीं आया। "पंखा बंद" जैसे बोलें।', 'Did not understand. Try "fan off".');
  String get voiceNoUnlock => _t('सुरक्षा के लिए ताला वॉइस से नहीं खुलेगा। टाइल दबाएँ।', 'For safety the lock cannot be opened by voice. Use the tile.');
  String get voiceUnavailable => _t('इस फ़ोन में वॉइस इनपुट उपलब्ध नहीं है।', 'Voice input is not available on this phone.');
  String voiceDone(String text) => _t('हो गया: “$text”', 'Done: “$text”');

  // rooms
  String get addRoom => _t('कमरा जोड़ें', 'Add Room');
  String get roomName => _t('कमरे का नाम', 'Room name');
  String get renameRoom => _t('नाम बदलें', 'Rename');
  String get deleteRoom => _t('कमरा हटाएँ', 'Delete room');
  String get moveToRoom => _t('किस कमरे में रखें?', 'Move to which room?');

  // groups & tiles
  String group(String g) {
    switch (g) {
      case 'Lighting':
        return _t('लाइटिंग', 'Lighting');
      case 'Climate':
        return _t('क्लाइमेट', 'Climate');
      case 'Security':
        return _t('सुरक्षा', 'Security');
      case 'Scenes':
        return _t('सीन और मोड', 'Scenes & Modes');
      case 'Other':
        return _t('अन्य', 'Other');
      default:
        return g;
    }
  }

  String get tempSensor => _t('तापमान सेंसर', 'Temp Sensor');
  String get routines => _t('रूटीन', 'Routines');
  String get moodDinner => _t('मूड सीन: डिनर', 'Mood Scene: Dinner');
  String get dnd => _t('DND मोड', 'DND Mode');
  String get securityCam => _t('सिक्योरिटी कैम', 'Security Cam');
  String get addControl => _t('कंट्रोल जोड़ें', 'Add Control');
  String get addControlHelp => _t(
      'नया कंट्रोल जोड़ने के लिए ऊपर के “डिवाइस जोड़ें” बटन का इस्तेमाल करें।',
      'To add a control, use the “Add Device” buttons above.');
  String get capOn => _t('चालू', 'On');
  String get capOff => _t('बंद', 'Off');
  String get capLocked => _t('लॉक्ड', 'Locked');
  String get capUnlocked => _t('अनलॉक्ड', 'Unlocked');
  String get capMotion => _t('मोशन', 'Motion');
  String get capClear => _t('शांत', 'Clear');
  String get capRun => _t('चलाएँ', 'Run');
  String get capView => _t('देखें', 'View');
  String humidityText(int percent) => _t('नमी $percent%', 'Humidity $percent%');
  String unlockQuestion(String name) => _t('“$name” अनलॉक करें?', 'Unlock “$name”?');
  String get unlock => _t('अनलॉक', 'Unlock');
  String get cameraNotSet => _t('ESP32 फ़र्मवेयर में CAMERA_URL खाली है।', 'CAMERA_URL is empty in the ESP32 firmware.');

  // scenes
  String get routinesTitle => _t('रूटीन चुनें', 'Choose a routine');
  String get sceneAllOff => _t('सब बंद', 'All off');
  String get sceneGoodNight => _t('गुड नाइट (सब बंद + ताला)', 'Good night (all off + lock)');
  String get sceneDinner => _t('डिनर', 'Dinner');
  String sceneName(String id) {
    switch (id) {
      case 'alloff':
        return sceneAllOff;
      case 'goodnight':
        return sceneGoodNight;
      case 'dinner':
        return sceneDinner;
      default:
        return id;
    }
  }

  // energy
  String energyTitle(String room) => _t('ऊर्जा खपत ($room)', 'Energy Consumption ($room)');
  String energyNow(int watts) => _t('अभी: $watts W', 'Now: $watts W');
  String get smartPlugs => _t('स्मार्ट प्लग', 'Smart Plugs');
  String get energyNote => _t('अनुमानित', 'Estimated');

  // settings additions
  String get apiKey => _t('API key (वैकल्पिक)', 'API key (optional)');
  String get apiKeyNone => _t('सेट नहीं है', 'Not set');
  String get apiKeyHelp => _t('फ़र्मवेयर में API_KEY डाला हो तो वही यहाँ डालें।', 'Enter the same value as API_KEY in the firmware.');
  String get pollInterval => _t('लाइव अपडेट अंतराल', 'Live update interval');

  // ------------------------------------------------- Wi-Fi connection settings
  String get wifi => 'Wi-Fi';
  String get wifiSettings => _t('Wi-Fi कनेक्शन सेटिंग', 'Wi-Fi connection settings');
  String get wifiSettingsHelp => _t(
      'नया Wi-Fi चुनें, कनेक्शन स्थिति और IP देखें, Wi-Fi रीसेट करें',
      'Choose a new Wi-Fi, see status and IP, reset Wi-Fi');
  String get wifiStatusTitle => _t('कनेक्शन स्थिति', 'Connection status');
  String get wifiConnectedLabel => _t('जुड़ा हुआ', 'Connected');
  String get wifiNotConnected => _t('किसी Wi-Fi से नहीं जुड़ा', 'Not connected to any Wi-Fi');
  String get espUnreachable => _t('ESP32 तक पहुँच नहीं हो रही', "Can't reach the ESP32");
  String get networkName => _t('नेटवर्क', 'Network');
  String get ipAddress => _t('IP एड्रेस', 'IP address');
  String get signal => _t('सिग्नल', 'Signal');
  String get appAddress => _t('ऐप में ESP32 का पता', 'ESP32 address in the app');
  String setupHotspotOn(String name) => _t('सेटअप हॉटस्पॉट चालू: $name', 'Setup hotspot on: $name');
  String get setupHotspotNote => _t(
      'ESP32 अभी अपने सेटअप हॉटस्पॉट पर है। नीचे से घर का Wi-Fi चुनें।',
      'The ESP32 is on its own setup hotspot. Pick your home Wi-Fi below.');
  String get scanNetworks => _t('Wi-Fi नेटवर्क खोजें', 'Scan for Wi-Fi networks');
  String get scanning => _t('खोज रहे हैं…', 'Scanning…');
  String get availableNetworks => _t('उपलब्ध नेटवर्क', 'Available networks');
  String get noNetworks => _t('कोई नेटवर्क नहीं मिला। फिर से खोजें।', 'No networks found. Scan again.');
  String get scanFailed => _t('नेटवर्क की सूची नहीं मिल सकी।', 'Could not get the network list.');
  String get otherNetwork => _t('दूसरा नेटवर्क (नाम खुद डालें)', 'Other network (type the name)');
  String get wifiName => _t('Wi-Fi का नाम (SSID)', 'Wi-Fi name (SSID)');
  String get wifiPassword => _t('Wi-Fi पासवर्ड', 'Wi-Fi password');
  String get wifiPasswordOpenHint => _t('खुले नेटवर्क के लिए खाली छोड़ें', 'Leave empty for an open network');
  String get showPassword => _t('पासवर्ड दिखाएँ', 'Show password');
  String get hidePassword => _t('पासवर्ड छिपाएँ', 'Hide password');
  String get connect => _t('कनेक्ट करें', 'Connect');
  String connectTo(String ssid) => _t('“$ssid” से कनेक्ट करें', 'Connect to “$ssid”');
  String get ssidRequired => _t('Wi-Fi का नाम डालें।', 'Enter the Wi-Fi name.');
  String get ssidTooLong => _t('Wi-Fi का नाम 32 बाइट से लंबा नहीं हो सकता।', 'The Wi-Fi name cannot be longer than 32 bytes.');
  String get passwordRequired => _t('इस नेटवर्क का पासवर्ड डालें।', 'Enter the password for this network.');
  String get passwordLength => _t('पासवर्ड 8 से 63 अक्षरों का होना चाहिए।', 'The password must be 8 to 63 characters.');
  String get connectingTitle => _t('ESP32 नए Wi-Fi से जुड़ रहा है…', 'ESP32 is joining the new Wi-Fi…');
  String get connectingHelp => _t(
      'इसमें 30 सेकंड तक लग सकते हैं। इस दौरान ऐप से कनेक्शन कुछ देर के लिए टूट सकता है।',
      'This can take up to 30 seconds. The app may lose contact for a moment.');
  String get connectedOk => _t('कनेक्ट हो गया', 'Connected');
  String newIpIs(String ip) => _t('ESP32 का IP: $ip', 'ESP32 IP: $ip');
  String get addressUpdated => _t('ऐप में ESP32 का पता नए IP पर अपडेट कर दिया गया है।', 'The ESP32 address in the app was updated to the new IP.');
  String get fixedIpTip => _t(
      'हर बार वही IP पाने के लिए राउटर में ESP32 के लिए IP रिज़र्व (DHCP reservation) कर दें।',
      'To always get the same IP, reserve an IP for the ESP32 in your router (DHCP reservation).');
  String get connectFailed => _t(
      'ESP32 नए Wi-Fi से नहीं जुड़ सका। नाम और पासवर्ड जाँचें। वह पुराने Wi-Fi पर लौट गया है।',
      'The ESP32 could not join the new Wi-Fi. Check the name and password. It went back to the old network.');
  String get timeoutText => _t('समय समाप्त हो गया। फिर कोशिश करें।', 'Timed out. Please try again.');
  String get movedTitle => _t('ESP32 शायद नए Wi-Fi पर चला गया', 'The ESP32 has probably moved to the new Wi-Fi');
  String get movedHelp => _t(
      'अपने फ़ोन को भी उसी नए Wi-Fi से जोड़ें। फिर ESP32 का नया IP (राउटर की डिवाइस लिस्ट या Arduino Serial Monitor में दिखता है) यहाँ डालें।',
      "Connect your phone to the same new Wi-Fi, then enter the ESP32's new IP (shown in your router's device list or the Arduino Serial Monitor).");
  String get enterNewIp => _t('नया IP डालें', 'Enter new IP');
  String get close => _t('बंद करें', 'Close');
  String get wifiReset => _t('Wi-Fi रीसेट करें', 'Reset Wi-Fi');
  String get wifiResetHelp => _t(
      'ESP32 में सेव किया हुआ Wi-Fi मिट जाएगा और वह सेटअप मोड में रीस्टार्ट होगा। आपके डिवाइस की सूची बनी रहेगी।',
      'The Wi-Fi saved in the ESP32 will be erased and it will restart in setup mode. Your device list is kept.');
  String get wifiResetDone => _t('Wi-Fi रीसेट हो गया', 'Wi-Fi was reset');
  String get setupModeTitle => _t('ESP32 सेटअप मोड', 'ESP32 setup mode');
  String get setupModeHelp => _t(
      'ESP32 किसी Wi-Fi से न जुड़ पाए (या रीसेट हो) तो वह अपना हॉटस्पॉट "SmartHome-Setup" चालू कर देता है (पासवर्ड: 12345678)। फ़ोन के Wi-Fi में उसे चुनें, फिर नीचे का बटन दबाकर यहीं से घर का Wi-Fi चुनें। अगर कनेक्ट न हो तो फ़ोन का मोबाइल डेटा बंद करके देखें।',
      'If the ESP32 cannot join a Wi-Fi (or was reset) it starts its own hotspot "SmartHome-Setup" (password: 12345678). Join it in your phone Wi-Fi list, then use the button below and choose your home Wi-Fi here. If it does not connect, try turning off mobile data.');
  String get useSetupAddress => _t('ऐप को सेटअप पते (192.168.4.1) से जोड़ें', 'Point the app to the setup address (192.168.4.1)');
  String get setupAddressSet => _t('ऐप अब 192.168.4.1 से जुड़ेगा।', 'The app now uses 192.168.4.1.');
  String get oldFirmwareWifi => errFirmware;

  // ------------------------------------------- add / edit / remove devices
  String get addBulb => _t('बल्ब जोड़ें', 'Add Bulb');
  String get addFan => _t('पंखा जोड़ें', 'Add Fan');
  String get addAnyDevice => _t('डिवाइस जोड़ें', 'Add Device');
  String get ctlCreate => _t('डिवाइस बनाएँ', 'Create Device');
  String get ctlEdit => _t('डिवाइस बदलें', 'Edit device');
  String get ctlRoom => _t('कमरा', 'Room');
  String get ctlIcon => _t('आइकन', 'Icon');
  String get ctlPin => _t('GPIO पिन', 'GPIO pin');
  String get ctlWatts => _t('बिजली खपत (वाट, वैकल्पिक)', 'Power (watts, optional)');
  String get ctlWattsHelp => _t('ऊर्जा खपत के अनुमान के लिए।', 'Used for the energy estimate.');
  String ctlPinUsed(String name) => _t('इस्तेमाल में: $name', 'In use: $name');
  String get ctlNoPin => _t('कोई खाली GPIO पिन नहीं बचा।', 'No free GPIO pin left.');
  String ctlLimit(int max) => _t('डिवाइस की सीमा पूरी हो गई (अधिकतम $max)।', 'Device limit reached (max $max).');
  String get nameTooLong => _t('नाम बहुत लंबा है (हिंदी में लगभग 15 अक्षर तक)।', 'The name is too long (about 15 Hindi letters).');
  String get ctlAdded => _t('डिवाइस जुड़ गया', 'Device added');
  String get ctlSaved => _t('डिवाइस अपडेट हो गया', 'Device updated');
  String get ctlDeleted => _t('डिवाइस हटा दिया गया', 'Device removed');
  String ctlDeleteQ(String name) => _t('“$name” हटाएँ?', 'Remove “$name”?');
  String get ctlDeleteHelp => _t('यह ESP32 में सेव की हुई सूची से भी हट जाएगा।', 'It is also removed from the list saved on the ESP32.');
  String get ctlMenuEdit => _t('बदलें (नाम, आइकन, GPIO)', 'Edit (name, icon, GPIO)');
  String get ctlMenuMove => _t('दूसरे कमरे में ले जाएँ', 'Move to another room');
  String get ctlMenuDelete => _t('डिवाइस हटाएँ', 'Remove device');

  String iconName(String key) {
    switch (key) {
      case 'light':
        return _t('बल्ब', 'Bulb');
      case 'fan':
        return _t('पंखा', 'Fan');
      case 'heater':
        return _t('हीटर', 'Heater');
      case 'curtain':
        return _t('पर्दा', 'Curtain');
      case 'purifier':
        return _t('प्यूरीफायर', 'Purifier');
      case 'siren':
        return _t('सायरन', 'Siren');
      case 'lock':
        return _t('ताला', 'Lock');
      case 'pir':
        return _t('PIR सेंसर', 'PIR sensor');
      default:
        return _t('प्लग', 'Plug');
    }
  }

  /// Turns the ESP32's English error text into the app language.
  String fwError(String detail) {
    switch (detail) {
      case 'this GPIO pin is already used':
        return _t('यह GPIO पिन किसी दूसरे डिवाइस में लगा है।', 'This GPIO pin is used by another device.');
      case 'this GPIO pin cannot be used':
        return _t('यह GPIO पिन इस्तेमाल नहीं हो सकता।', 'This GPIO pin cannot be used.');
      case 'device limit reached':
        return _t('डिवाइस की सीमा पूरी हो गई।', 'Device limit reached.');
      case 'name required':
        return nameRequired;
      case 'unknown device':
        return _t('यह डिवाइस ESP32 में नहीं मिला। रीफ़्रेश करें।', 'The ESP32 does not know this device. Refresh.');
      case 'unknown icon':
      case 'this icon does not fit the device type':
        return _t('यह आइकन इस डिवाइस के लिए सही नहीं है।', 'This icon does not fit this device.');
      case 'already connecting':
        return _t('ESP32 पहले से नए Wi-Fi से जुड़ रहा है।', 'The ESP32 is already joining a Wi-Fi.');
      case 'DND mode is on':
        return _t('DND मोड चालू है।', 'DND mode is on.');
      default:
        return detail;
    }
  }
}
