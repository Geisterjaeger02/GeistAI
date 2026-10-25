// GeistAI - user-interface strings (the assistant itself answers in any
// language the speech server understands; this is only the device chrome).
#pragma once
#include <stdint.h>

namespace geist {

enum UiLang : uint8_t { UI_EN = 0, UI_DE, UI_ES, UI_FR, UI_LANG_COUNT };

enum Str : uint8_t {
  S_HOLD_TO_TALK,
  S_DOUBLE_FOR_MENU,
  S_LISTENING,
  S_RELEASE_TO_SEND,
  S_TAP_TO_SEND,
  S_THINKING,
  S_SPEAKING,
  S_CLICK_TO_STOP,
  S_CONNECTING_WIFI,
  S_CONNECTING_SERVER,
  S_STARTING,
  S_READY,
  S_WELCOME,
  S_SETTINGS,
  S_SPEECH_LANG,
  S_UI_LANG,
  S_VOLUME,
  S_VOICE_REPLY,
  S_CLEAR_CHAT,
  S_BACK,
  S_ON,
  S_OFF,
  S_AUTO,
  S_ERR_WIFI,
  S_ERR_SERVER,
  S_ERR_NOTHING_HEARD,
  S_ERR_GENERIC,
  S_DONE,
  S_DEVICE_INFO,
  S_COUNT
};

static const char* const kStrings[UI_LANG_COUNT][S_COUNT] = {
    // English
    {"Hold or tap to talk", "Double-click: settings", "Listening…", "Release to send",
     "Tap to send", "Thinking…", "Speaking…", "Click to stop", "Connecting to Wi-Fi…",
     "Looking for server…", "Starting…", "Ready", "Hi! Hold the knob and just talk to me.",
     "Settings", "Speech language", "Menu language", "Volume", "Voice replies", "Clear chat",
     "Back", "On", "Off", "Auto", "No Wi-Fi connection", "Server not reachable",
     "I didn't hear anything", "Something went wrong", "Done", "Device info"},
    // Deutsch
    {"Halten oder tippen zum Sprechen", "Doppelklick: Einstellungen", "Ich höre zu…",
     "Loslassen zum Senden", "Tippen zum Senden", "Denke nach…", "Spreche…",
     "Klicken zum Stoppen", "Verbinde mit WLAN…", "Suche Server…", "Starte…", "Bereit",
     "Hallo! Halte den Knopf gedrückt und sprich einfach mit mir.", "Einstellungen",
     "Sprache (Sprechen)", "Menüsprache", "Lautstärke", "Sprachantwort", "Chat löschen",
     "Zurück", "An", "Aus", "Auto", "Kein WLAN", "Server nicht erreichbar",
     "Ich habe nichts gehört", "Etwas ist schiefgelaufen", "Fertig", "Geräteinfo"},
    // Español
    {"Mantén o toca para hablar", "Doble clic: ajustes", "Escuchando…", "Suelta para enviar",
     "Toca para enviar", "Pensando…", "Hablando…", "Clic para parar", "Conectando al Wi-Fi…",
     "Buscando servidor…", "Iniciando…", "Listo",
     "¡Hola! Mantén pulsado el botón y háblame.", "Ajustes", "Idioma de voz",
     "Idioma del menú", "Volumen", "Respuesta por voz", "Borrar chat", "Atrás", "Sí", "No",
     "Auto", "Sin Wi-Fi", "Servidor no disponible", "No he oído nada", "Algo salió mal",
     "Hecho", "Info del dispositivo"},
    // Français
    {"Maintiens ou touche pour parler", "Double-clic : réglages", "J'écoute…",
     "Relâche pour envoyer", "Touche pour envoyer", "Je réfléchis…", "Je parle…",
     "Clic pour arrêter", "Connexion au Wi-Fi…", "Recherche du serveur…", "Démarrage…", "Prêt",
     "Salut ! Maintiens le bouton et parle-moi.", "Réglages", "Langue parlée",
     "Langue du menu", "Volume", "Réponse vocale", "Effacer le chat", "Retour", "Oui", "Non",
     "Auto", "Pas de Wi-Fi", "Serveur injoignable", "Je n'ai rien entendu",
     "Une erreur est survenue", "Terminé", "Infos appareil"},
};

static const char* const kUiLangNames[UI_LANG_COUNT] = {"English", "Deutsch", "Español",
                                                        "Français"};

inline const char* tr(uint8_t lang, Str s) {
  if (lang >= UI_LANG_COUNT) lang = UI_EN;
  return kStrings[lang][s];
}

// Spoken languages offered in the settings menu. "auto" lets the server
// detect the language from your voice (Whisper does this very reliably).
struct SpeechLang {
  const char* code;  // ISO 639-1 sent to the server
  const char* label; // shown in the UI
};

static const SpeechLang kSpeechLangs[] = {
    {"auto", "Auto"},       {"de", "Deutsch"},   {"en", "English"},  {"es", "Español"},
    {"fr", "Français"},     {"it", "Italiano"},  {"pt", "Português"}, {"nl", "Nederlands"},
    {"pl", "Polski"},       {"tr", "Türkçe"},
};
static const uint8_t kSpeechLangCount = sizeof(kSpeechLangs) / sizeof(kSpeechLangs[0]);

}  // namespace geist
