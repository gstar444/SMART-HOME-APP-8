import 'package:flutter/services.dart';

import 'models.dart';

/// Android's built-in speech dialog, opened through MainActivity.kt.
/// No extra Flutter plugin (and no microphone permission) is needed because
/// the system recognizer UI does the listening.
const MethodChannel _voiceChannel = MethodChannel('esp32_controller/voice');

class VoiceUnavailable implements Exception {}

/// Opens the speech dialog once and returns what the user said (null = cancelled).
Future<String?> listenOnce({required bool hindi, required String prompt}) async {
  try {
    return await _voiceChannel.invokeMethod<String>('listen', {
      'locale': hindi ? 'hi-IN' : 'en-IN',
      'prompt': prompt,
    });
  } on MissingPluginException {
    throw VoiceUnavailable();
  } on PlatformException catch (e) {
    if (e.code == 'unavailable') throw VoiceUnavailable();
    return null;
  }
}

class VoiceAction {
  const VoiceAction({
    this.scene,
    this.set = const <String, bool>{},
    this.lockBlocked = false,
  });

  /// Scene id to run ("alloff", "goodnight", "dinner"), or null.
  final String? scene;

  /// device id -> desired state (for locks: true = locked).
  final Map<String, bool> set;

  /// The user asked to unlock a door by voice. We never do that.
  final bool lockBlocked;
}

const Map<String, List<String>> _aliases = {
  'light': ['light', 'lights', 'lamp', 'bulb', 'batti', 'लाइट', 'लैंप', 'बत्ती'],
  'fan': ['fan', 'pankha', 'pankhe', 'पंखा', 'पंखे'],
  'heater': ['heater', 'geyser', 'हीटर', 'गीजर'],
  'curtain': ['curtain', 'curtains', 'parda', 'parde', 'पर्दा', 'परदा', 'पर्दे'],
  'purifier': ['purifier', 'प्यूरीफायर', 'प्यूरिफायर'],
  'siren': ['siren', 'alarm', 'सायरन', 'अलार्म'],
  'lock': ['lock', 'door', 'tala', 'taala', 'darwaza', 'ताला', 'दरवाजा', 'दरवाज़ा'],
  'plug': ['plug', 'socket', 'प्लग', 'सॉकेट'],
};

const Set<String> _onWords = {
  'on', 'start', 'open', 'chalu', 'chala', 'chalao', 'jala', 'jalao',
  'चालू', 'ऑन', 'चला', 'चलाओ', 'जला', 'जलाओ', 'खोल', 'खोलो',
};
const Set<String> _offWords = {
  'off', 'stop', 'close', 'band', 'bandh', 'bujha', 'bujhao',
  'बंद', 'बन्द', 'ऑफ', 'ऑफ़', 'बुझा', 'बुझाओ',
};
const Set<String> _unlockWords = {
  'unlock', 'open', 'khol', 'kholo', 'खोल', 'खोलो', 'खोलिए',
};

final RegExp _wordRe = RegExp(r'[\p{L}\p{M}]+', unicode: true);

/// Turns a spoken sentence into an action. Understands English, Hindi
/// (Devanagari) and Hinglish, e.g. "light on", "पंखा बंद करो", "heater chalu".
/// Returns null when nothing in the sentence matches a device or scene.
VoiceAction? parseVoice(String raw, List<Esp32Device> devices) {
  final text = raw.toLowerCase().trim();
  if (text.isEmpty) return null;
  final tokens = _wordRe.allMatches(text).map((m) => m.group(0)!).toList();
  bool hasAny(Iterable<String> words) => tokens.any(words.contains);
  bool saysPhrase(List<String> phrases) => phrases.any(text.contains);

  // Scenes first.
  if (saysPhrase(['all off', 'everything off', 'sab band', 'sab bandh', 'सब बंद', 'सब बन्द', 'सभी बंद'])) {
    return const VoiceAction(scene: 'alloff');
  }
  if (saysPhrase(['good night', 'goodnight', 'गुड नाइट', 'शुभ रात्रि'])) {
    return const VoiceAction(scene: 'goodnight');
  }
  if (saysPhrase(['dinner', 'डिनर'])) {
    return const VoiceAction(scene: 'dinner');
  }

  final wantsOn = hasAny(_onWords);
  final wantsOff = hasAny(_offWords);
  final wantsUnlock = hasAny(_unlockWords);

  final result = <String, bool>{};
  var lockBlocked = false;

  // When a device is called by its own name ("bulb 2 on") only that device is
  // meant - not every bulb in the house.
  bool nameOf(Esp32Device d) =>
      d.name.isNotEmpty && text.contains(d.name.toLowerCase());
  final anyNamed = devices.any((d) => d.controllable && nameOf(d));

  for (final d in devices) {
    if (!d.controllable) continue;
    final nameHit = nameOf(d);
    if (anyNamed && !nameHit) continue;
    final aliasHit = (_aliases[d.icon] ?? const <String>[]).any(tokens.contains) ||
        (d.isLock && tokens.contains('unlock'));
    if (!nameHit && !aliasHit) continue;

    if (d.isLock) {
      if (wantsUnlock) {
        lockBlocked = true; // never unlock by voice
      } else if (wantsOff || tokens.contains('lock') || tokens.contains('ताला')) {
        result[d.id] = true; // locking is always safe
      }
      continue;
    }
    if (!wantsOn && !wantsOff) continue;
    result[d.id] = wantsOn && !wantsOff;
  }

  if (result.isEmpty && !lockBlocked) return null;
  return VoiceAction(set: result, lockBlocked: lockBlocked);
}
