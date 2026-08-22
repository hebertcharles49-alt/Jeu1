import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// État global : progression (victoires par jeu) + réglages.
/// Tout est sauvegardé localement sur l'appareil, rien ne sort du téléphone.
class AppState extends ChangeNotifier {
  AppState._();
  static final AppState instance = AppState._();

  static const gameIds = ['circuit', 'etats_eau', 'chaine'];

  late SharedPreferences _prefs;
  bool soundOn = true;
  final Map<String, int> _wins = {};

  Future<void> load() async {
    _prefs = await SharedPreferences.getInstance();
    soundOn = _prefs.getBool('soundOn') ?? true;
    for (final id in gameIds) {
      _wins[id] = _prefs.getInt('wins_$id') ?? 0;
    }
  }

  int winsFor(String gameId) => _wins[gameId] ?? 0;

  /// Étoiles affichées : plafonnées à 3.
  int starsFor(String gameId) => winsFor(gameId).clamp(0, 3);

  /// Stade d'évolution de la créature liée au jeu :
  /// 0 = pas capturée, 1..3 = stades d'évolution.
  int stageFor(String gameId) => winsFor(gameId).clamp(0, 3);

  int get totalStars =>
      gameIds.fold(0, (sum, id) => sum + starsFor(id));

  Future<void> recordWin(String gameId) async {
    _wins[gameId] = winsFor(gameId) + 1;
    await _prefs.setInt('wins_$gameId', _wins[gameId]!);
    notifyListeners();
  }

  Future<void> setSound(bool value) async {
    soundOn = value;
    await _prefs.setBool('soundOn', value);
    notifyListeners();
  }

  Future<void> resetProgress() async {
    for (final id in gameIds) {
      _wins[id] = 0;
      await _prefs.remove('wins_$id');
    }
    notifyListeners();
  }
}
