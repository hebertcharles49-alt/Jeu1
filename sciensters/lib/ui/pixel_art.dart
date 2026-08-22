import 'package:flutter/material.dart';

/// Dessine un sprite pixel art depuis une grille de caractères.
/// '.' = transparent, les autres caractères sont cherchés dans [palette].
class PixelArt extends StatelessWidget {
  final List<String> grid;
  final Map<String, Color> palette;
  final double size;
  final bool silhouette;

  const PixelArt({
    super.key,
    required this.grid,
    required this.palette,
    this.size = 96,
    this.silhouette = false,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: CustomPaint(
        painter: _PixelPainter(grid, palette, silhouette),
      ),
    );
  }
}

class _PixelPainter extends CustomPainter {
  final List<String> grid;
  final Map<String, Color> palette;
  final bool silhouette;

  _PixelPainter(this.grid, this.palette, this.silhouette);

  @override
  void paint(Canvas canvas, Size size) {
    if (grid.isEmpty) return;
    final cols = grid.map((r) => r.length).reduce((a, b) => a > b ? a : b);
    final rows = grid.length;
    final px = size.width / cols;
    final py = size.height / rows;
    final cell = px < py ? px : py;
    // Centrage du sprite dans la zone.
    final offsetX = (size.width - cell * cols) / 2;
    final offsetY = (size.height - cell * rows) / 2;
    final paint = Paint();

    for (var y = 0; y < rows; y++) {
      final row = grid[y];
      for (var x = 0; x < row.length; x++) {
        final ch = row[x];
        if (ch == '.') continue;
        final color =
            silhouette ? const Color(0xFF546E7A) : (palette[ch] ?? Colors.pink);
        paint.color = color;
        canvas.drawRect(
          Rect.fromLTWH(
            offsetX + x * cell,
            offsetY + y * cell,
            cell + 0.5,
            cell + 0.5,
          ),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(_PixelPainter oldDelegate) =>
      oldDelegate.grid != grid || oldDelegate.silhouette != silhouette;
}
