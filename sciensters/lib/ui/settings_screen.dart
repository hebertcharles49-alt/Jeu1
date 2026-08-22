import 'package:flutter/material.dart';

import '../core/app_state.dart';

/// Réglages : sons, remise à zéro, à propos.
class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF2F5F7),
      appBar: AppBar(
        title: const Text('⚙️ Réglages',
            style: TextStyle(fontWeight: FontWeight.bold)),
        backgroundColor: const Color(0xFF78909C),
        foregroundColor: Colors.white,
      ),
      body: SafeArea(
        child: ListenableBuilder(
          listenable: AppState.instance,
          builder: (context, _) {
            final app = AppState.instance;
            return ListView(
              padding: const EdgeInsets.all(16),
              children: [
                Container(
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: SwitchListTile(
                    title: const Text('Sons et vibrations',
                        style: TextStyle(fontSize: 18)),
                    secondary: Text(app.soundOn ? '🔔' : '🔕',
                        style: const TextStyle(fontSize: 28)),
                    value: app.soundOn,
                    onChanged: (v) => app.setSound(v),
                  ),
                ),
                const SizedBox(height: 14),
                Container(
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: ListTile(
                    leading:
                        const Text('🧹', style: TextStyle(fontSize: 28)),
                    title: const Text('Tout recommencer',
                        style: TextStyle(fontSize: 18)),
                    subtitle: const Text(
                        'Efface les étoiles et les Sciensters capturés'),
                    onTap: () => _confirmReset(context),
                  ),
                ),
                const SizedBox(height: 24),
                const Text(
                  'Sciensters v0.1\n'
                  'Un jeu pour explorer la physique, la chimie et les '
                  'sciences de la vie en s\'amusant.\n\n'
                  'Sans publicité, sans internet, sans chrono. 💚',
                  textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 15, color: Colors.blueGrey,
                      height: 1.4),
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  void _confirmReset(BuildContext context) {
    showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
        title: const Text('Tout recommencer ?'),
        content: const Text(
            'Les étoiles et les Sciensters capturés seront effacés. '
            'Es-tu sûr ?',
            style: TextStyle(fontSize: 16)),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Non, je garde tout !',
                style: TextStyle(fontSize: 16)),
          ),
          TextButton(
            onPressed: () {
              AppState.instance.resetProgress();
              Navigator.of(context).pop();
            },
            child: const Text('Oui, tout effacer',
                style: TextStyle(fontSize: 16, color: Colors.redAccent)),
          ),
        ],
      ),
    );
  }
}
