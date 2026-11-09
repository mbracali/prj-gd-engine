// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The port's PC wording (GD02-154): GH2's strings that name the Xbox 360,
// Xbox Live, consoles, storage devices or sign-in, rewritten for PC as
// gd-game-01 does (gd-game-01/engine/src/ui/pc_strings.cpp), in each of GH2's
// languages. Plus the GD-Engine loading tips, which join the tip pool.
#include "scene/pc_strings.h"

#include <cstring>

namespace scene {
namespace {

struct PcString {
    const char* token;
    const char* text[5];  // eng fre deu ita esl
};

const PcString kPcStrings[] = {
    {"signin_changed_msg",
     {"The save data changed outside the game. As a result, the game has restarted.",
      "Les données de sauvegarde ont changé en dehors du jeu. Le jeu a donc redémarré.",
      "Die Speicherdaten wurden außerhalb des Spiels geändert. Das Spiel wurde deshalb neu gestartet.",
      "I dati di salvataggio sono cambiati fuori dal gioco. Il gioco è stato quindi riavviato.",
      "Los datos guardados han cambiado fuera del juego. Por eso, el juego se ha reiniciado."}},
    {"sigin_to_live_title",
     {"LEADERBOARDS", "CLASSEMENTS", "BESTENLISTEN", "CLASSIFICHE", "MARCADORES"}},
    {"sigin_to_live_msg",
     {"Online leaderboards are not available in GD-Engine.",
      "Les classements en ligne ne sont pas disponibles dans GD-Engine.",
      "Online-Bestenlisten sind in GD-Engine nicht verfügbar.",
      "Le classifiche online non sono disponibili in GD-Engine.",
      "Los marcadores en línea no están disponibles en GD-Engine."}},
    {"downloads_shop_desc",
     {"Downloaded songs are not available in GD-Engine.",
      "Les chansons téléchargées ne sont pas disponibles dans GD-Engine.",
      "Heruntergeladene Songs sind in GD-Engine nicht verfügbar.",
      "Le canzoni scaricate non sono disponibili in GD-Engine.",
      "Las canciones descargadas no están disponibles en GD-Engine."}},
    {"mc_enable_autosave",
     {"You must save in order for autosave to become active.",
      "Tu dois sauvegarder pour activer la sauvegarde automatique.",
      "Sie müssen speichern, damit das automatische Speichern aktiv wird.",
      "Devi salvare per attivare il salvataggio automatico.",
      "Debes guardar para que se active el guardado automático."}},
    {"mc_autosave_device_changed",
     {"The GD-Engine save folder changed since the last save. Please save again, or disable autosave.",
      "Le dossier de sauvegarde de GD-Engine a changé depuis la dernière sauvegarde. Sauvegarde à nouveau ou désactive la sauvegarde automatique.",
      "Der GD-Engine-Speicherordner hat sich seit dem letzten Speichern geändert. Bitte erneut speichern oder das automatische Speichern deaktivieren.",
      "La cartella di salvataggio di GD-Engine è cambiata dall'ultimo salvataggio. Salva di nuovo o disattiva il salvataggio automatico.",
      "La carpeta de guardado de GD-Engine ha cambiado desde la última vez. Vuelve a guardar o desactiva el guardado automático."}},
    {"mc_checking",
     {"Checking the save data for the game. Don't turn off the system.",
      "Vérification des données de sauvegarde. N'éteins pas le système.",
      "Speicherdaten werden überprüft. Schalten Sie das System nicht aus.",
      "Controllo dei dati di salvataggio. Non spegnere il sistema.",
      "Comprobando los datos guardados. No apagues el sistema."}},
    {"mc_multiple_saves",
     {"More than one GD-Engine save was found. Please choose one, or continue without loading.",
      "Plusieurs sauvegardes GD-Engine ont été trouvées. Choisis-en une ou continue sans charger.",
      "Mehrere GD-Engine-Speicherstände wurden gefunden. Bitte wählen Sie einen aus oder fahren Sie ohne Laden fort.",
      "Sono stati trovati più salvataggi di GD-Engine. Scegline uno o continua senza caricare.",
      "Se han encontrado varias partidas de GD-Engine. Elige una o continúa sin cargar."}},
    {"mc_not_signed_in",
     {"GD-Engine could not find a safe way to save files. If you continue, your progress won't be saved.",
      "GD-Engine n'a pas trouvé de moyen sûr d'enregistrer. Si tu continues, ta progression ne sera pas sauvegardée.",
      "GD-Engine hat keinen sicheren Speicherort gefunden. Wenn Sie fortfahren, wird Ihr Fortschritt nicht gespeichert.",
      "GD-Engine non ha trovato un modo sicuro per salvare. Se continui, i tuoi progressi non verranno salvati.",
      "GD-Engine no ha encontrado una forma segura de guardar. Si continúas, tu progreso no se guardará."}},
    {"mc_confirm_not_signed_in",
     {"You have chosen to continue without saving. Any progress you make will not be saved.",
      "Tu as choisi de continuer sans sauvegarder. Ta progression ne sera pas sauvegardée.",
      "Sie haben sich entschieden, ohne Speichern fortzufahren. Ihr Fortschritt wird nicht gespeichert.",
      "Hai scelto di continuare senza salvare. I tuoi progressi non verranno salvati.",
      "Has elegido continuar sin guardar. Tu progreso no se guardará."}},
    {"mc_load_failed",
     {"LOAD FAILED: the GD-Engine save data could not be read.",
      "ÉCHEC DU CHARGEMENT : les données de sauvegarde GD-Engine sont illisibles.",
      "LADEN FEHLGESCHLAGEN: Die GD-Engine-Speicherdaten konnten nicht gelesen werden.",
      "CARICAMENTO NON RIUSCITO: impossibile leggere i dati di salvataggio di GD-Engine.",
      "ERROR DE CARGA: no se han podido leer los datos guardados de GD-Engine."}},
    {"mc_saving_long",
     {"Saving data... Do not shut down or reset system.",
      "Sauvegarde en cours... N'éteins pas et ne redémarre pas le système.",
      "Daten werden gespeichert... System nicht ausschalten oder neu starten.",
      "Salvataggio in corso... Non spegnere né riavviare il sistema.",
      "Guardando datos... No apagues ni reinicies el sistema."}},
    {"mc_save_not_enough_space",
     {"Insufficient space. Provide more free space on the disk the game is running from.",
      "Espace insuffisant. Libère de l'espace sur le disque où le jeu est installé.",
      "Nicht genügend Speicherplatz. Geben Sie auf dem Laufwerk des Spiels Speicherplatz frei.",
      "Spazio insufficiente. Libera spazio sul disco da cui è avviato il gioco.",
      "Espacio insuficiente. Libera espacio en el disco desde el que se ejecuta el juego."}},
    {"mc_storage_changed",
     {"The GD-Engine save data changed outside the game. You are being redirected to the Main Menu.",
      "Les données de sauvegarde GD-Engine ont changé en dehors du jeu. Retour au menu principal.",
      "Die GD-Engine-Speicherdaten wurden außerhalb des Spiels geändert. Sie werden zum Hauptmenü weitergeleitet.",
      "I dati di salvataggio di GD-Engine sono cambiati fuori dal gioco. Verrai riportato al menu principale.",
      "Los datos guardados de GD-Engine han cambiado fuera del juego. Volviendo al menú principal."}},
    {"mc_storage_device_missing",
     {"No GD-Engine save data found. Loading failed.",
      "Aucune sauvegarde GD-Engine trouvée. Échec du chargement.",
      "Keine GD-Engine-Speicherdaten gefunden. Laden fehlgeschlagen.",
      "Nessun salvataggio di GD-Engine trovato. Caricamento non riuscito.",
      "No se han encontrado datos guardados de GD-Engine. Error de carga."}},
    {"mc_save_storage_device_pulled",
     {"SAVE ERROR: Data was not saved successfully.",
      "ERREUR DE SAUVEGARDE : les données n'ont pas été enregistrées.",
      "SPEICHERFEHLER: Die Daten wurden nicht gespeichert.",
      "ERRORE DI SALVATAGGIO: i dati non sono stati salvati.",
      "ERROR AL GUARDAR: los datos no se han guardado."}},
    {"mc_load_storage_device_pulled",
     {"LOAD FAILED: the GD-Engine save data could not be read.",
      "ÉCHEC DU CHARGEMENT : les données de sauvegarde GD-Engine sont illisibles.",
      "LADEN FEHLGESCHLAGEN: Die GD-Engine-Speicherdaten konnten nicht gelesen werden.",
      "CARICAMENTO NON RIUSCITO: impossibile leggere i dati di salvataggio di GD-Engine.",
      "ERROR DE CARGA: no se han podido leer los datos guardados de GD-Engine."}},
    {"mc_choose_device",
     {"Choose Save Data", "Choisir la sauvegarde", "Speicherdaten wählen", "Scegli il salvataggio", "Elegir partida guardada"}},
    {"mc_signin", {"Save", "Sauvegarder", "Speichern", "Salva", "Guardar"}},
    {"sc_instructions",
     {"Press the strum bar on your guitar controller, or use the D-pad on your gamepad to move your desired input device onto your side of the screen.",
      "Utilise la touche de médiator sur la manette guitare ou bien la croix directionnelle de ta manette pour faire passer le périphérique d'entrée choisi sur ton côté de l'écran.",
      "Drücken Sie den Anschlagschalter Ihres Gitarren-Controllers oder benutzen Sie das Steuerkreuz Ihres Gamepads, um das von Ihnen gewünschte Eingabegerät auf Ihre Seite des Bildschirms zu bewegen.",
      "Premi la barra della pennata sulla chitarra, oppure usa la croce direzionale del gamepad per spostare la periferica di input sul tuo lato dello schermo.",
      "Pulsa la barra de toque en tu mando de guitarra o utiliza la cruceta de tu mando para mover el dispositivo de entrada deseado a tu lado de la pantalla."}},
    // The GD-Engine loading tips (gd-game-01's, in English in every language).
    {"gd_loading_tip1",
     {"GD-Engine Devs: This game saved my life back in the day, very dark times where the POWER OF ROCK helped me to not shut down the light "
      "within. GD-Engine is labor of love, giving back to the community what the community gave to me.\n"
      "Thanks for all the heroes at REDOCTANE, HARMONIX, MTV GAMES and ACTIVISION!"}},
    {"gd_loading_tip2", {"GD-Engine Devs: Do you know that I play drums as good as the drummer in GD-Engine looks like? Pretty bad right?"}},
    {"gd_loading_tip3", {"GD-Engine Devs: Can you guess how many parts of the code were vibe coded?"}},
    {"gd_loading_tip4",
     {"GD-Engine Devs: Yeah, I know... You should not leave important decisions to your drummer... But also, keep an eye on the bassist... "
      "I don't trust this guys... Look how GROOVY they are"}},
    {"gd_loading_tip5", {"GD-Engine Devs: The original game came out in 2006... This project is from 2026... 20 years... I'm old.."}},
    {"gd_loading_tip6",
     {"GD-Engine Devs: Thanks to my friends who I message non stop during each milestone of this project: Adler, Lucas, Colin, Mauricio, "
      "Geovane, Caio, Joao, Gabriel...\nYOU GUYS ROCK! (Don't block me)"}},
};

int languageIndex(const std::string& language) {
    static const char* const kLangs[] = {"eng", "fre", "deu", "ita", "esl"};
    for (int i = 0; i < 5; ++i)
        if (language == kLangs[i]) return i;
    return 0;
}

}  // namespace

const char* const kPcLoadingTips[] = {"gd_loading_tip1", "gd_loading_tip2", "gd_loading_tip3",
                                      "gd_loading_tip4", "gd_loading_tip5", "gd_loading_tip6"};
const int kPcLoadingTipCount = int(sizeof kPcLoadingTips / sizeof kPcLoadingTips[0]);
const char* const kDroppedLoadingTips[] = {"loading_tip10", "loading_tip13"};  // Xbox Live leaderboards
const int kDroppedLoadingTipCount = int(sizeof kDroppedLoadingTips / sizeof kDroppedLoadingTips[0]);

void applyPcStrings(std::map<std::string, std::string>& strings, const std::string& language) {
    const int li = languageIndex(language);
    for (const PcString& s : kPcStrings) {
        const char* text = s.text[li] ? s.text[li] : s.text[0];
        strings[s.token] = text;
    }
}

}  // namespace scene
