import 'mqtt_models.dart';
import 'mqtt_service.dart';
import 'secure_storage.dart';
import 'strings.dart';

/// Strings added in version 4 (cloud connection, boards, multi-board dashboard).
/// Hindi first, English second, like the rest of the app.
extension CloudStrings on Tr {
  String _x(String h, String e) => hi ? h : e;

  // ------------------------------------------------------------- errors
  String get errNotClaimed => _x(
      'यह बोर्ड अभी claim नहीं हुआ है। Settings → Boards में “Add board” से इसे जोड़ें।',
      'This board has not been claimed yet. Use “Add board” in Settings → Boards.');
  String get errRateLimited => _x(
      'बहुत ज़्यादा गलत कोशिशें। एक मिनट बाद फिर कोशिश करें।', 'Too many failed attempts. Try again in a minute.');
  String get localOnly => _x(
      'यह बदलाव बोर्ड से लोकल नेटवर्क पर जुड़े होने पर ही हो सकता है (सुरक्षा के लिए क्लाउड से नहीं)।',
      'This change needs a direct connection to the board on your local network (it is not allowed through the cloud, for security).');
  String get remoteNotAllowed =>
      _x('इस डिवाइस को क्लाउड से कंट्रोल करने की अनुमति नहीं है।', 'This device is not allowed to be controlled through the cloud.');
  String get boardOffline => _x('यह बोर्ड अभी ऑफ़लाइन है।', 'This board is offline right now.');

  /// Turns a board's short error code into a sentence.
  String boardError(String? code) {
    switch (code) {
      case null:
        return errServer;
      case 'unknown_device':
        return _x('डिवाइस नहीं मिला।', 'Device not found.');
      case 'disabled':
        return _x('यह डिवाइस बंद (disabled) है।', 'This device is disabled.');
      case 'pin_fault':
        return _x('इस डिवाइस का GPIO पिन इस बोर्ड पर सुरक्षित नहीं है, इसलिए इसे चलाया नहीं गया।',
            "This device's GPIO pin is not safe on this board, so it is never driven.");
      case 'read_only':
        return _x('यह सेंसर है, इसे चालू/बंद नहीं कर सकते।', 'This is a sensor; it can not be switched.');
      case 'dnd':
        return _x('Do-Not-Disturb चालू है, सायरन नहीं बजेगा।', 'Do-Not-Disturb is on, so the siren will not start.');
      case 'remote_not_allowed':
        return remoteNotAllowed;
      case 'remote_config_disabled':
        return _x('इस बोर्ड पर क्लाउड से नाम/कमरे बदलना बंद है।', 'Changing names/rooms through the cloud is turned off on this board.');
      case 'rate_limited':
        return _x('बहुत तेज़ कमांड। थोड़ा रुककर कोशिश करें।', 'Too many commands. Wait a moment.');
      case 'timeout':
        return _x('बोर्ड ने जवाब नहीं दिया।', 'The board did not answer.');
      case 'not_connected':
      case 'connection_lost':
      case 'publish_failed':
        return notConnected;
      case 'pin_not_allowed':
        return _x('यह GPIO पिन इस बोर्ड पर इस्तेमाल के लिए सुरक्षित नहीं है।', 'This GPIO pin is not safe to use on this board.');
      case 'pin_duplicate':
        return _x('यह GPIO पिन पहले से किसी और डिवाइस के लिए है।', 'This GPIO pin is already used by another device.');
      case 'pin_reserved':
        return _x('यह पिन सेंसर/बटन के लिए आरक्षित है।', 'This pin is reserved for the sensor or the reset button.');
      case 'device_id_duplicate':
        return _x('इस नाम की ID पहले से है।', 'A device with this id already exists.');
      case 'device_in_use':
        return _x('यह डिवाइस किसी scene/schedule/automation में इस्तेमाल हो रहा है। पहले उसे हटाएँ।',
            'This device is used by a scene, schedule or automation. Remove that first.');
      case 'last_room':
        return _x('आख़िरी कमरा हटाया नहीं जा सकता।', 'The last room can not be deleted.');
      case 'bad_code':
        return _x('Claim code गलत है। बोर्ड के Serial Monitor में दिखा कोड डालें।',
            "The claim code is wrong. Use the code shown in the board's Serial Monitor.");
      case 'already_claimed':
        return boardAlreadyClaimed;
      case 'not_claimed':
        return errNotClaimed;
      case 'save_failed':
        return _x('बोर्ड सेटिंग सेव नहीं कर पाया।', 'The board could not save the setting.');
      case 'busy':
        return _x('बोर्ड अभी व्यस्त है, थोड़ी देर बाद कोशिश करें।', 'The board is busy, try again shortly.');
      case 'ssid_length':
      case 'password_length':
        return _x('Wi-Fi नाम/पासवर्ड की लंबाई सही नहीं है (पासवर्ड 8–63 अक्षर)।',
            'Wi-Fi name or password has the wrong length (password: 8-63 characters).');
      case 'mqtt_port':
        return _x('क्लाउड प्रोवाइडर के लिए TLS पोर्ट 8883 ज़रूरी है।', 'The cloud providers require TLS port 8883.');
      case 'mqtt_host':
      case 'mqtt_username':
      case 'mqtt_password':
      case 'mqtt_home_id':
        return _x('क्लाउड सेटिंग अधूरी या गलत है।', 'The cloud settings are incomplete or invalid.');
      case 'action_would_unlock':
        return _x('सुरक्षा के लिए scene/schedule/automation ताला नहीं खोल सकते।', 'For safety, scenes, schedules and automations can never unlock a door.');
      case 'timezone':
        return _x('टाइम ज़ोन सही नहीं है (उदाहरण: IST-5:30)।', 'The time zone is not valid (example: IST-5:30).');
      case 'unknown_scene':
        return _x('यह routine बोर्ड पर मौजूद नहीं है।', 'This routine does not exist on the board.');
      default:
        return '$errServer ($code)';
    }
  }

  // --------------------------------------------------------------- cloud
  String get cloudTitle => _x('क्लाउड (MQTT)', 'Cloud (MQTT)');
  String get cloudSecurityNote => _x(
      'कनेक्शन हमेशा एन्क्रिप्टेड (TLS) रहता है और ब्रोकर का सर्टिफ़िकेट जाँचा जाता है; इसे बंद करने का कोई विकल्प नहीं है। पासवर्ड सिर्फ़ फ़ोन के secure storage में रहता है।',
      'The connection is always encrypted (TLS) and the broker certificate is verified; there is no option to turn that off. The password is kept only in the phone\'s secure storage.');
  String get cloudProvider => _x('प्रोवाइडर', 'Provider');
  String providerName(CloudProvider p) => switch (p) {
        CloudProvider.hivemq => 'HiveMQ Cloud',
        CloudProvider.emqx => 'EMQX Cloud',
        CloudProvider.custom => _x('अपना ब्रोकर (custom)', 'Own broker (custom)'),
      };
  String get cloudHost => _x('ब्रोकर होस्ट', 'Broker host');
  String get cloudPort => _x('पोर्ट', 'Port');
  String get cloudPortFixed => _x('TLS, पोर्ट 8883', 'TLS, port 8883');
  String get cloudUsername => _x('यूज़रनेम', 'Username');
  String get cloudUsernameHelp =>
      _x('ऐप के लिए अलग क्रेडेंशियल बनाएँ', 'Create a separate credential for the app');
  String get cloudPassword => _x('पासवर्ड', 'Password');
  String get cloudHomeId => _x('Home id', 'Home id');
  String get cloudHomeIdHelp => _x('हर बोर्ड में यही होना चाहिए (छोटे अक्षर, अंक, - _)', 'Must match every board (lower-case letters, digits, - _)');
  String get cloudCustomCa => _x('अतिरिक्त भरोसेमंद CA (PEM)', 'Extra trusted CA (PEM)');
  String get cloudCustomCaHelp => _x('सिर्फ़ अपने ब्रोकर के private CA के लिए। यह जाँच बंद नहीं करता।', 'Only for a private CA of your own broker. It never disables verification.');
  String get cloudSave => _x('सेव करके कनेक्ट करें', 'Save & connect');
  String get cloudTest => _x('कनेक्शन टेस्ट', 'Test connection');
  String get cloudTestOk => _x('कनेक्शन सफल: TLS सर्टिफ़िकेट सही है और लॉगिन स्वीकार हुआ।', 'Connected: the TLS certificate verified and the login was accepted.');
  String get cloudSaved => _x('सेव हुआ। कनेक्ट हो रहा है…', 'Saved. Connecting…');
  String get cloudRemove => _x('हटाएँ', 'Remove');
  String get cloudRemoveTitle => _x('क्लाउड कनेक्शन हटाएँ?', 'Remove the cloud connection?');
  String get cloudRemoveBody => _x('फ़ोन से ब्रोकर का यूज़रनेम/पासवर्ड मिट जाएगा। बोर्ड लोकल नेटवर्क पर चलते रहेंगे।', 'The broker username and password are deleted from this phone. Boards keep working on the local network.');
  String get cloudNotConfigured => _x('क्लाउड सेट नहीं है', 'Cloud is not set up');
  String cloudBoardsSeen(int n) => _x('$n बोर्ड दिख रहे हैं', '$n board(s) seen');
  String cloudPhase(MqttPhase p) => switch (p) {
        MqttPhase.connected => _x('क्लाउड से जुड़ा है', 'Connected to the cloud'),
        MqttPhase.connecting => _x('कनेक्ट हो रहा है…', 'Connecting…'),
        MqttPhase.reconnecting => _x('दोबारा कनेक्ट हो रहा है…', 'Reconnecting…'),
        MqttPhase.failed => _x('क्लाउड से कनेक्ट नहीं हो पाया', 'Could not reach the cloud'),
        MqttPhase.disabled => _x('क्लाउड बंद है', 'Cloud is off'),
      };
  String get cloudMenuHelp => _x('घर के बाहर से कंट्रोल के लिए HiveMQ/EMQX ब्रोकर', 'HiveMQ/EMQX broker for control from anywhere');
  String get boardsMenuHelp => _x('बोर्ड जोड़ें, token और क्लाउड लॉगिन', 'Add boards, tokens and cloud login');

  // -------------------------------------------------------------- boards
  String get boardsTitle => _x('बोर्ड', 'Boards');
  String get boardsHelp => _x(
      'हर कमरे का अपना ESP32 बोर्ड हो सकता है। नया बोर्ड जोड़ने के लिए उसके Serial Monitor में दिखा claim code चाहिए।',
      "Each room can have its own ESP32 board. To add a new one you need the claim code shown in its Serial Monitor.");
  String get noBoardsYet => _x('अभी कोई बोर्ड नहीं', 'No boards yet');
  String get addBoard => _x('बोर्ड जोड़ें', 'Add board');
  String get addBoardNote => _x(
      'नया बोर्ड: उसके hotspot “SmartHome-xxxxxx” से जुड़ें (पासवर्ड = claim code), पता 192.168.4.1 रखें और Claim code डालें।\nपहले से claim हुआ बोर्ड (जैसे दूसरे फ़ोन में): बोर्ड का IP और उसका Token डालें; Claim code खाली छोड़ें।',
      'New board: join its hotspot “SmartHome-xxxxxx” (password = claim code), keep address 192.168.4.1 and enter the claim code.\nBoard that is already claimed (for example on your other phone): enter the board\'s IP and its token and leave the claim code empty.');
  String get claim => _x('Claim करें', 'Claim');
  String get boardAddress => _x('बोर्ड का पता (IP)', 'Board address (IP)');
  String get claimCode => _x('Claim code', 'Claim code');
  String get boardName => _x('बोर्ड का नाम', 'Board name');
  String get badAddress => _x('पता सही नहीं है।', 'That address is not valid.');
  String get boardAlreadyClaimed => _x(
      'यह बोर्ड पहले ही claim हो चुका है। अगर token खो गया है तो बोर्ड का BOOT बटन 8 सेकंड दबाकर credentials रीसेट करें।',
      'This board is already claimed. If the token was lost, hold the board\'s BOOT button for 8 seconds to reset its credentials.');
  String get claimDone => _x('बोर्ड claim हो गया। अब Settings → Wi-Fi से इसे अपने Wi-Fi से जोड़ें।', 'Board claimed. Next, join it to your Wi-Fi in Settings → Wi-Fi.');
  String get boardNotIdentified => _x('बोर्ड ID अभी पता नहीं', 'Board id not known yet');
  String get unnamedBoard => _x('बोर्ड', 'Board');
  String sourceLabel(BoardSource s) => switch (s) {
        BoardSource.local => _x('लोकल कनेक्शन', 'Local connection'),
        BoardSource.cloud => _x('क्लाउड कनेक्शन', 'Cloud connection'),
        BoardSource.none => _x('ऑफ़लाइन', 'Offline'),
      };
  String get useLocally => _x('लोकल कंट्रोल के लिए चुनें', 'Use for local control');
  String get setAddress => _x('पता बदलें', 'Change address');
  String get enterToken => _x('Token डालें', 'Enter token');
  String get enterTokenNote => _x('अगर आपके पास इस बोर्ड का access token पहले से है (जैसे दूसरे फ़ोन से)।', 'If you already have this board\'s access token (for example from another phone).');
  String get accessToken => _x('Access token', 'Access token');
  String get badToken => _x('Token 32–128 अक्षर का होना चाहिए (A–Z, a–z, 0–9, - _)।', 'A token has 32-128 characters (A-Z, a-z, 0-9, - _).');
  String get tokenSaved => _x('Token सेव हुआ।', 'Token saved.');
  String get changeToken => _x('Token बदलें', 'Change token');
  String get showToken => _x('Token देखें / कॉपी करें', 'Show / copy token');
  String get tokenShareNote => _x(
      'दूसरे फ़ोन में: Settings → Boards → Add board → बोर्ड का IP और यह token डालें (Claim code खाली छोड़ें)। जिसके पास यह token है वह आपके नेटवर्क पर इस बोर्ड को कंट्रोल कर सकता है, इसलिए इसे सार्वजनिक न करें।',
      'On the other phone: Settings → Boards → Add board → enter the board\'s IP and this token (leave the claim code empty). Anyone who has this token can control this board on your network, so do not share it publicly.');
  String get tokenEditNote => _x(
      'नया token चुनें या बनवाएँ (32–128 अक्षर: A–Z, a–z, 0–9, - _)। बदलते ही पुराना token सभी फ़ोनों पर बंद हो जाएगा; नया token दूसरे फ़ोनों में डालना होगा।',
      'Choose a new token or let one be generated (32-128 characters: A-Z, a-z, 0-9, - _). The old token stops working on every phone as soon as you change it; the other phones need the new one.');
  String get generateNew => _x('नया बनाएँ', 'Generate new');
  String get copy => _x('कॉपी', 'Copy');
  String get copied => _x('कॉपी हो गया', 'Copied');
  String get tokenChanged => _x('Token बदल गया।', 'Token changed.');
  String get tokenChangedTitle => _x('नया token', 'New token');
  String get tokenChangedNote => _x(
      'इसे अभी कॉपी करके दूसरे फ़ोनों में डालें (Add board → Token)। पुराना token अब काम नहीं करेगा। बाद में भी “Token देखें / कॉपी करें” से देख सकते हैं।',
      'Copy it now and enter it on your other phones (Add board → Token). The old token no longer works. You can always see it again with “Show / copy token”.');
  String get noTokenStored => _x('इस बोर्ड का token इस फ़ोन में सेव नहीं है।', 'This phone has no token saved for this board.');
  String get accessTokenExisting => _x('Token (पहले से claim हुए बोर्ड के लिए)', 'Token (for a board that is already claimed)');
  String get needCodeOrToken => _x(
      'नया बोर्ड: Claim code डालें। पहले से claim हुआ बोर्ड: उसका Token डालें।',
      'New board: enter the claim code. Board already claimed: enter its token.');
  String get boardAdded => _x('बोर्ड जुड़ गया।', 'Board added.');
  String get boardNotClaimedYet => _x(
      'यह बोर्ड अभी claim नहीं हुआ है; Token की जगह Claim code डालें।',
      'This board has not been claimed yet; enter the claim code instead of a token.');
  String get addBoardOk => _x('जोड़ें', 'Add');
  String get sendCloudLogin => _x('बोर्ड को क्लाउड लॉगिन भेजें', 'Send cloud login to board');
  String get sendCloudLoginNote => _x(
      'हर बोर्ड के लिए ब्रोकर में अलग यूज़र बनाएँ (ऐप वाला यूज़र न दें)। यह लोकल कनेक्शन पर भेजा जाता है और बोर्ड से वापस कभी नहीं पढ़ा जा सकता।',
      'Create a separate broker user for each board (do not reuse the app\'s). It is sent over the local connection and can never be read back from the board.');
  String get send => _x('भेजें', 'Send');
  String get cloudLoginSent => _x('बोर्ड को क्लाउड लॉगिन मिल गया।', 'The board received the cloud login.');
  String get boardSettings => _x('नाम और टाइम ज़ोन', 'Name & time zone');
  String get timeZone => _x('टाइम ज़ोन', 'Time zone');
  String get timeZoneNote => _x('Schedules के लिए ज़रूरी। POSIX फ़ॉर्मेट, जैसे IST-5:30। खाली छोड़ने पर वही रहेगा।', 'Needed for schedules. POSIX format, for example IST-5:30. Leave a field empty to keep it.');
  String get removeBoardTitle => _x('बोर्ड हटाएँ?', 'Remove this board?');
  String get removeBoardBody => _x('सिर्फ़ इस फ़ोन से बोर्ड और उसका token हटेगा। बोर्ड खुद नहीं बदलेगा।', 'Removes the board and its token from this phone only. The board itself is not changed.');
  String get remove => _x('हटाएँ', 'Remove');
  String get saved => _x('सेव हुआ।', 'Saved.');

  // ----------------------------------------------------------- dashboard
  String get capOffline => _x('ऑफ़लाइन', 'Offline');
  String get cameraUrlTitle => _x('कैमरा का पता', 'Camera address');
  String get cameraUrlNone => _x('सेट नहीं है', 'Not set');
  String get cameraUrlHelp => _x(
      'ESP32-CAM जैसे कैमरे का http(s) पता। यह सिर्फ़ आप तय करते हैं, बोर्ड नहीं। http पते के लिए android/app/src/main/res/xml/network_security_config.xml में उसका होस्ट जोड़ना पड़ सकता है।',
      'The http(s) address of a camera such as an ESP32-CAM. Only you set it, never a board. A plain-http address may also need its host added to android/app/src/main/res/xml/network_security_config.xml.');
  String get cloudIconTip => _x('क्लाउड कनेक्शन', 'Cloud connection');
  String get roomsSyncNote => _x('कमरे सभी बोर्ड पर सिंक होते हैं।', 'Rooms are synchronised across all boards.');
  String get recoveryTitle => _x('Wi-Fi भूल गया', 'Wi-Fi forgotten');
  String recoveryBody(String hotspot, String code) => _x(
      'बोर्ड रीस्टार्ट होकर hotspot “$hotspot” बनाएगा। उसका पासवर्ड: $code\nइसे सुरक्षित लिख लें; यह दोबारा नहीं दिखेगा।',
      'The board restarts into the hotspot “$hotspot”. Its password: $code\nWrite it down; it is not shown again.');
}
