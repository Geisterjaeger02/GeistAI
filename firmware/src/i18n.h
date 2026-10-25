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
  S_HOLD_TO_POWER_OFF,
  S_GOODBYE,
  S_TYPE_PLACEHOLDER,
  S_HINT_TALK_CARDPUTER,
  S_HINT_MENU_CARDPUTER,
  S_HINT_TALK_STICK,
  S_HINT_MENU_STICK,
  S_WIFI_SETUP,
  S_SETUP_SCAN,
  S_SETUP_JOIN,
  S_SETUP_PASS,
  S_SETUP_OPEN,
  S_SETUP_SAVED,
  S_SETUP_CANCEL,
  S_COLOR,
  S_AI_NAME,
  S_AI_BRAIN,
  S_AI_LOCAL,
  S_AI_CLOUD,
  S_NAME_PLACEHOLDER,
  S_NOT_SET_UP,
  S_COUNT
};

static const char* const kStrings[UI_LANG_COUNT][S_COUNT] = {
    // English
    {"Hold to talk · tap to type", "Top button: settings", "Listening…", "Release to send",
     "Tap to send", "Thinking…", "Speaking…", "Click to stop", "Connecting to Wi-Fi…",
     "Looking for server…", "Starting…", "Ready", "Hi, I'm %s! Hold the knob and just talk to me.",
     "Settings", "Speech language", "Menu language", "Volume", "Voice replies", "Clear chat",
     "Back", "On", "Off", "Auto", "No Wi-Fi connection", "Server not reachable",
     "I didn't hear anything", "Something went wrong", "Done", "Device info", "Keep holding to turn off", "See you soon!", "Type a message…", "Hold G0 or Space: talk", "Type to write · Tab: settings", "Hold A: talk · A: type", "B: next · PWR: settings",
     "Wi-Fi setup", "Scan with your phone", "or join this Wi-Fi:", "Password", "then open", "Saved! Restarting…", "Press to cancel",
     "Color", "AI name", "AI model", "Local", "ChatGPT", "Name for your AI…", "not set up"},
    // Deutsch
    {"Halten: sprechen · Tippen: schreiben", "Obere Taste: Einstellungen", "Ich höre zu…",
     "Loslassen zum Senden", "Tippen zum Senden", "Denke nach…", "Spreche…",
     "Klicken zum Stoppen", "Verbinde mit WLAN…", "Suche Server…", "Starte…", "Bereit",
     "Hallo, ich bin %s! Halte den Knopf gedrückt und sprich einfach mit mir.", "Einstellungen",
     "Sprache (Sprechen)", "Menüsprache", "Lautstärke", "Sprachantwort", "Chat löschen",
     "Zurück", "An", "Aus", "Auto", "Kein WLAN", "Server nicht erreichbar",
     "Ich habe nichts gehört", "Etwas ist schiefgelaufen", "Fertig", "Geräteinfo", "Halten zum Ausschalten", "Bis bald!", "Nachricht schreiben…", "G0 oder Leertaste halten: sprechen", "Tippen: schreiben · Tab: Menü", "A halten: sprechen · A: schreiben", "B: weiter · PWR: Einstellungen",
     "WLAN einrichten", "Mit dem Handy scannen", "oder mit WLAN verbinden:", "Passwort", "dann öffnen", "Gespeichert! Neustart…", "Drücken: abbrechen",
     "Farbe", "Name der KI", "KI-Modell", "Lokal", "ChatGPT", "Name für deine KI…", "nicht eingerichtet"},
    // Español
    {"Mantén: hablar · Toca: escribir", "Botón superior: ajustes", "Escuchando…", "Suelta para enviar",
     "Toca para enviar", "Pensando…", "Hablando…", "Clic para parar", "Conectando al Wi-Fi…",
     "Buscando servidor…", "Iniciando…", "Listo",
     "¡Hola, soy %s! Mantén pulsado el botón y háblame.", "Ajustes", "Idioma de voz",
     "Idioma del menú", "Volumen", "Respuesta por voz", "Borrar chat", "Atrás", "Sí", "No",
     "Auto", "Sin Wi-Fi", "Servidor no disponible", "No he oído nada", "Algo salió mal",
     "Hecho", "Info del dispositivo", "Mantén para apagar", "¡Hasta pronto!", "Escribe un mensaje…", "Mantén G0 o Espacio: hablar", "Escribe · Tab: ajustes", "Mantén A: hablar · A: escribir", "B: siguiente · PWR: ajustes",
     "Configurar Wi-Fi", "Escanea con el móvil", "o conéctate a:", "Clave", "y abre", "¡Guardado! Reiniciando…", "Pulsa: cancelar",
     "Color", "Nombre de la IA", "Modelo de IA", "Local", "ChatGPT", "Nombre para tu IA…", "no configurado"},
    // Français
    {"Maintiens : parler · Touche : écrire", "Bouton du haut : réglages", "J'écoute…",
     "Relâche pour envoyer", "Touche pour envoyer", "Je réfléchis…", "Je parle…",
     "Clic pour arrêter", "Connexion au Wi-Fi…", "Recherche du serveur…", "Démarrage…", "Prêt",
     "Salut, je suis %s ! Maintiens le bouton et parle-moi.", "Réglages", "Langue parlée",
     "Langue du menu", "Volume", "Réponse vocale", "Effacer le chat", "Retour", "Oui", "Non",
     "Auto", "Pas de Wi-Fi", "Serveur injoignable", "Je n'ai rien entendu",
     "Une erreur est survenue", "Terminé", "Infos appareil", "Maintiens pour éteindre", "À bientôt !", "Écris un message…", "Maintiens G0/Espace : parler", "Écris · Tab : réglages", "Maintiens A : parler · A : écrire", "B : suivant · PWR : réglages",
     "Config. Wi-Fi", "Scanne avec ton mobile", "ou rejoins ce Wi-Fi :", "Mot de passe", "puis ouvre", "Enregistré ! Redémarrage…", "Appuie : annuler",
     "Couleur", "Nom de l'IA", "Modèle d'IA", "Local", "ChatGPT", "Nom de ton IA…", "non configuré"},
};

// Names of the colour themes (same order as geist::kThemes in ui.cpp).
static const char* const kThemeNames[UI_LANG_COUNT][6] = {
    {"Neon cyan", "Neon pink", "Neon green", "Violet", "Sunset", "Ice"},
    {"Neon-Cyan", "Neon-Pink", "Neon-Grün", "Violett", "Sonnenuntergang", "Eis"},
    {"Cian neón", "Rosa neón", "Verde neón", "Violeta", "Atardecer", "Hielo"},
    {"Cyan néon", "Rose néon", "Vert néon", "Violet", "Coucher de soleil", "Glace"},
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
    // added in v0.2 (keep the order: the setting is stored as an index)
    {"ru", "Русский"},      {"uk", "Українська"}, {"cs", "Čeština"},  {"sk", "Slovenčina"},
    {"sv", "Svenska"},      {"da", "Dansk"},      {"no", "Norsk"},    {"fi", "Suomi"},
    {"hu", "Magyar"},       {"ro", "Română"},     {"el", "Ελληνικά"}, {"ca", "Català"},
    {"sr", "Srpski"},       {"vi", "Tiếng Việt"}, {"id", "Indonesia"},
    // scripts the screen can't draw: named in Latin letters
    {"zh", "Chinese"},      {"ar", "Arabic"},     {"fa", "Farsi"},    {"hi", "Hindi"},
};
static const uint8_t kSpeechLangCount = sizeof(kSpeechLangs) / sizeof(kSpeechLangs[0]);

}  // namespace geist
