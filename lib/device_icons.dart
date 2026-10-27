import 'package:flutter/material.dart';

import 'dashboard_widgets.dart';

/// Icons a relay output can use. Every one of them is a plain on/off switch.
const List<String> switchIcons = <String>[
  'light',
  'fan',
  'heater',
  'curtain',
  'purifier',
  'siren',
  'plug',
];

/// Every icon the "Create Device" form offers.
const List<String> allIcons = <String>[...switchIcons, 'lock', 'pir'];

/// The ESP32 decides a device's type from its icon, exactly like this.
String typeForIcon(String icon) {
  if (icon == 'lock') return 'lock';
  if (icon == 'pir') return 'pir';
  return 'switch';
}

/// Icons that may be chosen for an existing device of [type]
/// (a relay cannot turn into a PIR sensor, so each type keeps its own list).
List<String> iconsForType(String type) {
  if (type == 'lock') return const <String>['lock'];
  if (type == 'pir') return const <String>['pir'];
  return switchIcons;
}

GlyphBuilder _iconGlyph(IconData icon) =>
    (color, size) => Icon(icon, color: color, size: size);

GlyphBuilder _shapeGlyph(String kind) =>
    (color, size) => Glyph(kind, color: color, size: size);

/// Picture for a device tile / icon chooser. Unknown keys show a power plug.
GlyphBuilder glyphFor(String key, bool on) {
  switch (key) {
    case 'light':
      return _iconGlyph(on ? Icons.lightbulb : Icons.lightbulb_outline);
    case 'fan':
      return _iconGlyph(Icons.toys);
    case 'heater':
      return _shapeGlyph('radiator');
    case 'curtain':
      return _shapeGlyph('blinds');
    case 'purifier':
      return _shapeGlyph('purifier');
    case 'siren':
      return _iconGlyph(Icons.notifications_active);
    case 'lock':
      return _iconGlyph(on ? Icons.lock : Icons.lock_open);
    case 'pir':
      return (color, size) => Text(
            'PIR',
            style: TextStyle(
              fontSize: size * 0.5,
              fontWeight: FontWeight.w800,
              letterSpacing: 0.5,
              color: color,
            ),
          );
    default:
      return _iconGlyph(Icons.power);
  }
}
