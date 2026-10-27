import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import 'mqtt_models.dart';

/// What the app last knew about one board.
class CachedBoard {
  const CachedBoard({required this.inventory, required this.state, required this.savedAt});

  final BoardInventory inventory;
  final BoardState? state;
  final DateTime savedAt;
}

/// Remembers the last inventory/state of every board, so the dashboard can show the home immediately
/// at start-up (marked as not-live) instead of a spinner while the network answers.
///
/// Contents: room names, device names/icons and their last on/off state, temperature. No credentials,
/// tokens or passwords are ever written here. It lives in the app's private storage (auto-backup is off).
/// Anything unreadable, too old, or of another protocol version is simply ignored.
class SnapshotCache {
  SnapshotCache(this._prefs);

  final SharedPreferences _prefs;

  static const String _key = 'snapshot_cache_v1';
  static const int maxBoards = 16;
  static const Duration maxAge = Duration(days: 30);

  /// Synchronous: SharedPreferences is already in memory by the time the app starts.
  Map<String, CachedBoard> load({DateTime? now}) {
    final out = <String, CachedBoard>{};
    final raw = _prefs.getString(_key);
    if (raw == null) return out;
    try {
      final doc = jsonDecode(raw);
      if (doc is! Map || doc['boards'] is! Map) return out;
      final t = now ?? DateTime.now();
      for (final e in (doc['boards'] as Map).entries) {
        final v = e.value;
        if (out.length >= maxBoards || e.key is! String || v is! Map) continue;
        final inv = BoardInventory.tryParse(v['inventory']);
        final at = DateTime.fromMillisecondsSinceEpoch((v['savedAt'] as num?)?.toInt() ?? 0);
        if (inv == null || inv.board != e.key || t.difference(at) > maxAge) continue;
        out[e.key as String] = CachedBoard(inventory: inv, state: BoardState.tryParse(v['state']), savedAt: at);
      }
    } catch (_) {
      // a damaged cache is just an empty cache
    }
    return out;
  }

  String encode(Map<String, CachedBoard> boards) => jsonEncode({
        'boards': {
          for (final e in boards.entries.take(maxBoards))
            e.key: {
              'savedAt': e.value.savedAt.millisecondsSinceEpoch,
              'inventory': e.value.inventory.toJson(),
              if (e.value.state != null) 'state': e.value.state!.toJson(),
            },
        },
      });

  Future<void> save(Map<String, CachedBoard> boards) => _prefs.setString(_key, encode(boards));

  Future<void> clear() => _prefs.remove(_key);
}
