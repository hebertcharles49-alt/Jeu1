import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// État global : progression (niveaux par zone) + réglages.
/// Tout est sauvegardé localement sur l'appareil, rien ne sort du téléphone.
class AppState extends ChangeNotifier {
  AppState._();
  static final AppState instance = AppState._();

  static const zoneIds = ['physique', 'chimie', 'svt'];
  static const maxLevel = 5;

  late SharedPreferences _prefs;
  bool soundOn = true;
  final Map<String, int> _levels = {};

  Future<void> load() async {
    _prefs = await SharedPreferences.getInstance();
    soundOn = _prefs.getBool('soundOn') ?? true;
    for (final id in zoneIds) {
      _levels[id] = _prefs.getInt('level_$id') ?? 0;
    }
  }

  /// Plus haut niveau terminé dans la zone (0 = aucun, max 5).
  int levelReached(String zoneId) => _levels[zoneId] ?? 0;

  bool isLevelUnlocked(String zoneId, int level) =>
      level <= levelReached(zoneId) + 1;

  bool isLevelDone(String zoneId, int level) => level <= levelReached(zoneId);

  /// Stade d'évolution de la créature de la zone :
  /// 0 = pas capturée, 1 dès le niveau 1, 2 dès le niveau 3, 3 au niveau 5.
  int stageForZone(String zoneId) {
    final reached = levelReached(zoneId);
    if (reached >= maxLevel) return 3;
    if (reached >= 3) return 2;
    if (reached >= 1) return 1;
    return 0;
  }

  int get totalLevelsDone =>
      zoneIds.fold(0, (sum, id) => sum + levelReached(id));

  Future<void> completeLevel(String zoneId, int level) async {
    if (level > levelReached(zoneId)) {
      _levels[zoneId] = level;
      await _prefs.setInt('level_$zoneId', level);
      notifyListeners();
    }
  }

  Future<void> setSound(bool value) async {
    soundOn = value;
    await _prefs.setBool('soundOn', value);
    notifyListeners();
  }

  Future<void> resetProgress() async {
    for (final id in zoneIds) {
      _levels[id] = 0;
      await _prefs.remove('level_$id');
    }
    notifyListeners();
  }
}
