import 'dart:async';

import 'package:flutter/material.dart';

import 'strings.dart';

bool _isDark(BuildContext c) => Theme.of(c).brightness == Brightness.dark;

Color _muted(BuildContext c) =>
    Theme.of(c).colorScheme.onSurface.withValues(alpha: 0.62);

Color _border(BuildContext c) => _isDark(c)
    ? Colors.white.withValues(alpha: 0.10)
    : Colors.black.withValues(alpha: 0.07);

String _two(int v) => v.toString().padLeft(2, '0');

/// Soft "glass" card used for the clock, status boxes and energy panel.
class GlassCard extends StatelessWidget {
  const GlassCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(14),
    this.radius = 20,
    this.onTap,
    this.glow,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final double radius;
  final VoidCallback? onTap;
  final Color? glow; // soft coloured halo (dark theme only)

  @override
  Widget build(BuildContext context) {
    final dark = _isDark(context);
    return Container(
      decoration: BoxDecoration(
        color: dark ? Colors.white.withValues(alpha: 0.06) : Colors.white,
        borderRadius: BorderRadius.circular(radius),
        border: Border.all(color: _border(context)),
        boxShadow: [
          if (dark && glow != null)
            BoxShadow(color: glow!.withValues(alpha: 0.28), blurRadius: 26),
          if (!dark)
            BoxShadow(
              color: Colors.black.withValues(alpha: 0.05),
              blurRadius: 12,
              offset: const Offset(0, 4),
            ),
        ],
      ),
      child: Material(
        type: MaterialType.transparency,
        child: InkWell(
          borderRadius: BorderRadius.circular(radius),
          onTap: onTap,
          child: Padding(padding: padding, child: child),
        ),
      ),
    );
  }
}

/// Big digital clock + date, ticking every second.
class ClockCard extends StatefulWidget {
  const ClockCard({super.key, required this.tr, this.glow});

  final Tr tr;
  final Color? glow;

  @override
  State<ClockCard> createState() => _ClockCardState();
}

class _ClockCardState extends State<ClockCard> {
  late DateTime _now;
  Timer? _ticker;

  @override
  void initState() {
    super.initState();
    _now = DateTime.now();
    _ticker = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() => _now = DateTime.now());
    });
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final tr = widget.tr;
    final onSurface = Theme.of(context).colorScheme.onSurface;
    return GlassCard(
      glow: widget.glow,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '${_two(_now.hour)}:${_two(_now.minute)}',
            style: TextStyle(
              fontSize: 40,
              height: 1.1,
              fontWeight: FontWeight.w300,
              letterSpacing: 1,
              color: onSurface,
            ),
          ),
          const SizedBox(height: 6),
          Divider(height: 1, color: _border(context)),
          const SizedBox(height: 6),
          Text(
            '${tr.weekday(_now.weekday)}, ${_now.day} ${tr.month(_now.month)}',
            style: TextStyle(fontSize: 14, color: _muted(context)),
          ),
        ],
      ),
    );
  }
}

/// "Connected to: ..." / "ESP32 System State" boxes.
class StatusCard extends StatelessWidget {
  const StatusCard({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.ok,
    this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final bool ok;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return GlassCard(
      onTap: onTap,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      child: Row(
        children: [
          Icon(icon, size: 26, color: _muted(context)),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  title,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: theme.textTheme.titleSmall
                      ?.copyWith(fontWeight: FontWeight.w700),
                ),
                const SizedBox(height: 3),
                Row(
                  children: [
                    Container(
                      width: 8,
                      height: 8,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: ok
                            ? const Color(0xFF22C55E)
                            : const Color(0xFFEF4444),
                      ),
                    ),
                    const SizedBox(width: 6),
                    Flexible(
                      child: Text(
                        subtitle,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: theme.textTheme.bodySmall
                            ?.copyWith(color: _muted(context)),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class VoiceButton extends StatelessWidget {
  const VoiceButton({
    super.key,
    required this.label,
    required this.accent,
    required this.listening,
    required this.onTap,
  });

  final String label;
  final Color accent;
  final bool listening;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 84,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          GestureDetector(
            onTap: onTap,
            child: AnimatedContainer(
              duration: const Duration(milliseconds: 250),
              width: 64,
              height: 64,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: accent.withValues(alpha: 0.14),
                border: Border.all(color: accent.withValues(alpha: 0.65)),
                boxShadow: [
                  BoxShadow(
                    color: accent.withValues(alpha: listening ? 0.7 : 0.3),
                    blurRadius: listening ? 28 : 16,
                  ),
                ],
              ),
              child: Icon(
                listening ? Icons.hearing : Icons.mic,
                size: 30,
                color: accent,
              ),
            ),
          ),
          const SizedBox(height: 6),
          Text(
            label,
            maxLines: 2,
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 11.5, height: 1.15, color: _muted(context)),
          ),
        ],
      ),
    );
  }
}

/// Sun / moon switch. `dark` = current theme is dark.
class ThemeToggle extends StatelessWidget {
  const ThemeToggle({super.key, required this.dark, required this.onChanged});

  final bool dark;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    final muted = _muted(context);
    return GlassCard(
      padding: const EdgeInsets.all(12),
      onTap: () => onChanged(!dark),
      child: SizedBox(
        width: 72,
        height: 36,
        child: Stack(
          children: [
            Positioned.fill(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(18),
                  color: dark
                      ? Colors.black.withValues(alpha: 0.28)
                      : Colors.black.withValues(alpha: 0.07),
                ),
              ),
            ),
            Positioned(
              left: 10,
              top: 9,
              child: Icon(Icons.light_mode, size: 18, color: muted),
            ),
            Positioned(
              right: 10,
              top: 9,
              child: Icon(Icons.dark_mode, size: 18, color: muted),
            ),
            AnimatedPositioned(
              duration: const Duration(milliseconds: 220),
              curve: Curves.easeOut,
              left: dark ? 38 : 2,
              top: 2,
              child: Container(
                width: 32,
                height: 32,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: dark ? const Color(0xFF3A4458) : Colors.white,
                  boxShadow: [
                    BoxShadow(
                      color: Colors.black.withValues(alpha: 0.25),
                      blurRadius: 6,
                    ),
                  ],
                ),
                child: Icon(
                  dark ? Icons.dark_mode : Icons.light_mode,
                  size: 18,
                  color: dark ? Colors.white : const Color(0xFFF59E0B),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class RoomChip extends StatelessWidget {
  const RoomChip({
    super.key,
    required this.label,
    required this.selected,
    required this.accent,
    required this.onTap,
    this.onLongPress,
  });

  final String label;
  final bool selected;
  final Color accent;
  final VoidCallback onTap;
  final VoidCallback? onLongPress;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      onLongPress: onLongPress,
      behavior: HitTestBehavior.opaque,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 11),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(24),
          color: selected ? accent.withValues(alpha: 0.2) : Colors.transparent,
          border: Border.all(color: selected ? accent : _border(context)),
          boxShadow: selected
              ? [BoxShadow(color: accent.withValues(alpha: 0.35), blurRadius: 14)]
              : null,
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 15,
            fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
            color: selected
                ? Theme.of(context).colorScheme.onSurface
                : _muted(context),
          ),
        ),
      ),
    );
  }
}

/// Builds the picture inside a tile (a Material icon or a custom drawing).
typedef GlyphBuilder = Widget Function(Color color, double size);

/// One control: rounded square with a picture, label below, optional caption.
/// `span: 2` makes a wide tile (used for the lock). `outline` = active state
/// is drawn as a glowing outline instead of a solid fill (like the heater).
class DeviceTile extends StatelessWidget {
  const DeviceTile({
    super.key,
    required this.width,
    required this.glyph,
    required this.label,
    required this.accent,
    this.caption,
    this.active = false,
    this.outline = false,
    this.busy = false,
    this.dim = false,
    this.span = 1,
    this.onTap,
    this.onLongPress,
  });

  final double width; // width of ONE grid cell
  final GlyphBuilder glyph;
  final String label;
  final String? caption;
  final Color accent;
  final bool active;
  final bool outline;
  final bool busy;
  final bool dim;
  final int span;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;

  @override
  Widget build(BuildContext context) {
    final dark = _isDark(context);
    final scheme = Theme.of(context).colorScheme;
    final totalWidth = width * span + 12.0 * (span - 1);
    final boxHeight = width * 0.88;
    final boxWidth = span == 1 ? boxHeight : totalWidth;
    final filled = active && !outline;
    final fg = active ? Colors.white : scheme.onSurface.withValues(alpha: 0.72);
    final radius = BorderRadius.circular(22);

    final Widget inside;
    if (busy) {
      inside = SizedBox(
        width: 22,
        height: 22,
        child: CircularProgressIndicator(strokeWidth: 2.4, color: fg),
      );
    } else if (span == 1) {
      inside = glyph(fg, boxHeight * 0.46);
    } else {
      inside = Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          glyph(fg, boxHeight * 0.46),
          const SizedBox(width: 14),
          Flexible(
            child: Text(
              caption ?? label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: fg),
            ),
          ),
        ],
      );
    }

    return Opacity(
      opacity: dim ? 0.45 : 1,
      child: SizedBox(
        width: totalWidth,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            AnimatedContainer(
              duration: const Duration(milliseconds: 200),
              width: boxWidth,
              height: boxHeight,
              decoration: BoxDecoration(
                borderRadius: radius,
                gradient: filled
                    ? LinearGradient(
                        begin: Alignment.topLeft,
                        end: Alignment.bottomRight,
                        colors: [
                          Color.lerp(accent, Colors.white, 0.18) ?? accent,
                          accent,
                        ],
                      )
                    : null,
                color: filled
                    ? null
                    : (active
                        ? accent.withValues(alpha: 0.12)
                        : (dark ? Colors.white.withValues(alpha: 0.07) : Colors.white)),
                border: Border.all(
                  color: active ? accent : _border(context),
                  width: active ? 1.4 : 1,
                ),
                boxShadow: active
                    ? [
                        BoxShadow(
                          color: accent.withValues(alpha: filled ? 0.55 : 0.45),
                          blurRadius: 20,
                          offset: const Offset(0, 4),
                        ),
                      ]
                    : (dark
                        ? null
                        : [
                            BoxShadow(
                              color: Colors.black.withValues(alpha: 0.05),
                              blurRadius: 8,
                              offset: const Offset(0, 3),
                            ),
                          ]),
              ),
              child: Material(
                type: MaterialType.transparency,
                child: InkWell(
                  borderRadius: radius,
                  onTap: onTap,
                  onLongPress: onLongPress,
                  child: Center(child: inside),
                ),
              ),
            ),
            const SizedBox(height: 7),
            Text(
              label,
              maxLines: 2,
              textAlign: TextAlign.center,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 12,
                height: 1.15,
                fontWeight: FontWeight.w600,
                color: scheme.onSurface,
              ),
            ),
            if (span == 1 && caption != null && caption!.isNotEmpty) ...[
              const SizedBox(height: 2),
              Text(
                caption!,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 10.5,
                  fontWeight: FontWeight.w600,
                  color: active ? accent : _muted(context),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Outlined box that holds one group of tiles (Lighting, Climate, ...).
class GroupBox extends StatelessWidget {
  const GroupBox({
    super.key,
    required this.title,
    required this.child,
    required this.width,
  });

  final String title;
  final Widget child;
  final double width;

  @override
  Widget build(BuildContext context) {
    final dark = _isDark(context);
    return Container(
      width: width,
      padding: const EdgeInsets.fromLTRB(13, 10, 13, 16),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(24),
        color: dark ? Colors.white.withValues(alpha: 0.035) : Colors.white.withValues(alpha: 0.6),
        border: Border.all(color: _border(context)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            title,
            style: TextStyle(fontSize: 13, letterSpacing: 0.8, color: _muted(context)),
          ),
          const SizedBox(height: 12),
          child,
        ],
      ),
    );
  }
}

/// Hand-drawn pictures for things Material Icons does not have
/// (radiator, window blinds, candle, air purifier, floor plan).
class Glyph extends StatelessWidget {
  const Glyph(this.kind, {super.key, required this.color, required this.size});

  final String kind;
  final Color color;
  final double size;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: CustomPaint(painter: _GlyphPainter(kind, color)),
    );
  }
}

class _GlyphPainter extends CustomPainter {
  _GlyphPainter(this.kind, this.color);

  final String kind;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final s = size.width;
    Paint stroke(double w) => Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = s * w
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    final fill = Paint()
      ..color = color
      ..style = PaintingStyle.fill;
    Offset p(double x, double y) => Offset(s * x, s * y);

    switch (kind) {
      case 'radiator':
        final fin = stroke(0.10);
        for (var i = 0; i < 5; i++) {
          final x = 0.2 + i * 0.15;
          canvas.drawLine(p(x, 0.3), p(x, 0.78), fin);
        }
        canvas.drawLine(p(0.12, 0.2), p(0.88, 0.2), stroke(0.06));
        canvas.drawLine(p(0.12, 0.86), p(0.88, 0.86), stroke(0.06));
        break;
      case 'blinds':
        canvas.drawRRect(
          RRect.fromRectAndRadius(
            Rect.fromLTWH(s * 0.2, s * 0.16, s * 0.6, s * 0.68),
            Radius.circular(s * 0.04),
          ),
          stroke(0.06),
        );
        for (final y in [0.34, 0.48, 0.62]) {
          canvas.drawLine(p(0.28, y), p(0.72, y), stroke(0.05));
        }
        canvas.drawLine(p(0.66, 0.62), p(0.66, 0.76), stroke(0.03));
        canvas.drawCircle(p(0.66, 0.79), s * 0.03, fill);
        break;
      case 'candle':
        final flame = Path()
          ..moveTo(s * 0.5, s * 0.12)
          ..quadraticBezierTo(s * 0.66, s * 0.28, s * 0.5, s * 0.4)
          ..quadraticBezierTo(s * 0.34, s * 0.28, s * 0.5, s * 0.12);
        canvas.drawPath(flame, fill);
        canvas.drawRRect(
          RRect.fromRectAndRadius(
            Rect.fromLTWH(s * 0.38, s * 0.48, s * 0.24, s * 0.34),
            Radius.circular(s * 0.04),
          ),
          stroke(0.06),
        );
        canvas.drawLine(p(0.26, 0.88), p(0.74, 0.88), stroke(0.06));
        break;
      case 'purifier':
        canvas.drawRRect(
          RRect.fromRectAndRadius(
            Rect.fromLTWH(s * 0.28, s * 0.1, s * 0.44, s * 0.8),
            Radius.circular(s * 0.09),
          ),
          stroke(0.06),
        );
        canvas.drawCircle(p(0.5, 0.22), s * 0.035, fill);
        for (var r = 0; r < 4; r++) {
          for (var c = 0; c < 3; c++) {
            canvas.drawCircle(p(0.4 + c * 0.1, 0.42 + r * 0.11), s * 0.024, fill);
          }
        }
        break;
      case 'floorplan':
        final thin = stroke(0.045);
        canvas.drawRect(Rect.fromLTWH(s * 0.08, s * 0.1, s * 0.84, s * 0.8), thin);
        canvas.drawLine(p(0.42, 0.1), p(0.42, 0.46), thin);
        canvas.drawLine(p(0.42, 0.62), p(0.42, 0.9), thin);
        canvas.drawLine(p(0.08, 0.5), p(0.3, 0.5), thin);
        canvas.drawLine(p(0.42, 0.54), p(0.92, 0.54), thin);
        canvas.drawLine(p(0.68, 0.54), p(0.68, 0.9), thin);
        break;
    }
  }

  @override
  bool shouldRepaint(_GlyphPainter old) => old.kind != kind || old.color != color;
}

class SectionLabel extends StatelessWidget {
  const SectionLabel(this.text, {super.key});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 6, bottom: 12),
      child: Text(
        text.toUpperCase(),
        style: TextStyle(
          fontSize: 12,
          letterSpacing: 1.4,
          fontWeight: FontWeight.w700,
          color: _muted(context),
        ),
      ),
    );
  }
}

/// Energy panel (like the mock-up tooltip): title + kWh chip, usage bars,
/// plug icon + "Smart Plugs". Numbers are an ESTIMATE (relay on-time x watts).
class EnergyCard extends StatelessWidget {
  const EnergyCard({
    super.key,
    required this.tr,
    required this.room,
    required this.kwh,
    required this.watts,
    required this.series,
    required this.accent,
  });

  final Tr tr;
  final String room;
  final double kwh;
  final double watts;
  final List<double> series; // watts, oldest first
  final Color accent;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final muted = _muted(context);
    return GlassCard(
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  tr.energyTitle(room),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: theme.textTheme.titleSmall
                      ?.copyWith(fontWeight: FontWeight.w700),
                ),
              ),
              const SizedBox(width: 8),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: _border(context)),
                ),
                child: Text('kWh', style: TextStyle(fontSize: 11, color: muted)),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              _Bars(values: series, accent: accent),
              const Spacer(),
              Icon(Icons.power, size: 22, color: accent),
              const SizedBox(width: 6),
              Text(
                tr.smartPlugs,
                style: theme.textTheme.titleSmall
                    ?.copyWith(fontWeight: FontWeight.w600),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            '${kwh.toStringAsFixed(3)} kWh  ·  ${tr.energyNow(watts.round())}  ·  ${tr.energyNote}',
            style: theme.textTheme.bodySmall?.copyWith(color: muted),
          ),
        ],
      ),
    );
  }
}

class _Bars extends StatelessWidget {
  const _Bars({required this.values, required this.accent});

  final List<double> values;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    const count = 20;
    final padded = <double>[
      for (var i = 0; i < count - values.length; i++) 0,
      ...values.length > count ? values.sublist(values.length - count) : values,
    ];
    var peak = 1.0;
    for (final v in padded) {
      if (v > peak) peak = v;
    }
    return SizedBox(
      height: 48,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          for (final v in padded)
            Container(
              width: 4.5,
              height: 4 + 44 * (v / peak),
              margin: const EdgeInsets.only(left: 2),
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(2),
                color: v <= 0
                    ? _border(context)
                    : accent.withValues(alpha: 0.35 + 0.65 * (v / peak)),
              ),
            ),
        ],
      ),
    );
  }
}
