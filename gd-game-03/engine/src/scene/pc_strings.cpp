// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The port's PC wording (GD03-101): Encore's strings that name the memory
// card, its slots, the console or controller ports, rewritten for PC with
// gd-game-01's wording (gd-game-01/engine/src/ui/pc_strings.cpp) in each of
// Encore's languages. Encore keeps the PS2 text under "<token>_ps2" (the
// localize native looks there first, natives_meta.cpp), so both are written.
// Plus the GD-Engine loading tips, which join every tip pool.
#include "scene/pc_strings.h"

namespace scene {
namespace {

struct PcString {
    const char* token;
    const char* text[5];  // eng fre deu ita esl; nullptr: the English one
};

#define ERR_SPACE                                                                                                              \
    {"SAVE ERROR: Insufficient space to save data. Provide more free space on the disk the game is running from. Continue without saving?", \
     "ERREUR DE SAUVEGARDE : espace insuffisant. Libère de l'espace sur le disque d'où le jeu est lancé. Continuer sans sauvegarder ?", \
     "SPEICHERFEHLER: Nicht genügend Speicherplatz. Schaffe Platz auf dem Laufwerk, von dem das Spiel läuft. Ohne Speichern fortfahren?", \
     "ERRORE DI SALVATAGGIO: spazio insufficiente. Libera spazio sul disco da cui viene eseguito il gioco. Continuare senza salvare?", \
     "ERROR AL GUARDAR: espacio insuficiente. Libera espacio en el disco desde el que se ejecuta el juego. ¿Continuar sin guardar?"}
#define ERR_RETRY                                                                                                              \
    {"SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Select Retry to try again.",              \
     "ERREUR DE SAUVEGARDE : espace insuffisant, format corrompu ou erreur inconnue. Choisis Réessayer pour recommencer.",      \
     "SPEICHERFEHLER: Nicht genügend Speicherplatz, beschädigtes Format oder unbekannter Fehler. Wähle Wiederholen.",            \
     "ERRORE DI SALVATAGGIO: spazio insufficiente, formato danneggiato o errore sconosciuto. Seleziona Riprova per riprovare.",  \
     "ERROR AL GUARDAR: espacio insuficiente, formato dañado o error desconocido. Selecciona Reintentar para volver a intentarlo."}
#define NO_DATA                                                                                                                \
    {"Caution! No GD-Engine save data found. Continue without saving?",                                                        \
     "Attention ! Aucune sauvegarde GD-Engine trouvée. Continuer sans sauvegarder ?",                                          \
     "Achtung! Keine GD-Engine-Speicherdaten gefunden. Ohne Speichern fortfahren?",                                            \
     "Attenzione! Nessun salvataggio GD-Engine trovato. Continuare senza salvare?",                                            \
     "¡Atención! No se han encontrado datos guardados de GD-Engine. ¿Continuar sin guardar?"}
#define CTRL_LOSS                                                                                                              \
    {"Please make sure your Guitar Controller is connected.", "Vérifie que ta manette guitare est bien branchée.",             \
     "Bitte stelle sicher, dass dein Gitarren-Controller angeschlossen ist.", "Assicurati che la chitarra sia collegata.",     \
     "Asegúrate de que tu mando de guitarra está conectado."}

const PcString kPcStrings[] = {
    // Shown in normal use on PC.
    {"mc_checking",
     {"Checking the save data for the game. Do not remove the me... I mean... Don't turn off the system.",
      "Vérification des données de sauvegarde. Ne retire pas la carte mé... enfin... N'éteins pas le système.",
      "Speicherdaten werden geprüft. Entferne nicht die Memory C... äh... Schalte das System nicht aus.",
      "Verifica dei dati di salvataggio. Non rimuovere la memory c... cioè... Non spegnere il sistema.",
      "Comprobando los datos guardados. No retires la tarjeta de me... digo... No apagues el sistema."}},
    {"load_loading",
     {"Loading saved data... Do not shut down or reset system.",
      "Chargement des données... N'éteins pas et ne redémarre pas le système.",
      "Daten werden geladen... System nicht ausschalten oder neu starten.",
      "Caricamento dei dati... Non spegnere né riavviare il sistema.",
      "Cargando datos... No apagues ni reinicies el sistema."}},
    {"load_success",
     {"GD-Engine data loaded successfully.", "Données GD-Engine chargées.", "GD-Engine-Daten erfolgreich geladen.",
      "Dati GD-Engine caricati correttamente.", "Datos de GD-Engine cargados correctamente."}},
    {"bootup_create_save_check",
     {"No GD-Engine data was present on the system. GD-Engine uses an Autosave feature. Can we create a new save file now?",
      "Aucune donnée GD-Engine sur le système. GD-Engine utilise une sauvegarde automatique. Créer un fichier de sauvegarde maintenant ?",
      "Keine GD-Engine-Daten auf dem System. GD-Engine speichert automatisch. Jetzt eine Speicherdatei anlegen?",
      "Nessun dato GD-Engine presente nel sistema. GD-Engine usa il salvataggio automatico. Creare ora un file di salvataggio?",
      "No hay datos de GD-Engine en el sistema. GD-Engine usa el guardado automático. ¿Crear ahora un archivo de guardado?"}},
    {"bootup_create_save",
     {"Creating GD-Engine save file. Do not shut down or restart the system.",
      "Création du fichier de sauvegarde GD-Engine. N'éteins pas et ne redémarre pas le système.",
      "GD-Engine-Speicherdatei wird angelegt. System nicht ausschalten oder neu starten.",
      "Creazione del file di salvataggio GD-Engine. Non spegnere né riavviare il sistema.",
      "Creando el archivo de guardado de GD-Engine. No apagues ni reinicies el sistema."}},
    {"bootup_create_success",
     {"GD-Engine data created successfully! Let's rock!", "Données GD-Engine créées ! Que le rock commence !",
      "GD-Engine-Daten erfolgreich angelegt! Lass uns rocken!", "Dati GD-Engine creati! Si rocka!",
      "¡Datos de GD-Engine creados! ¡A rockear!"}},
    {"bootup_caution",
     {"Caution! GD-Engine uses an Autosave feature.\nWe could not find a safe way to save files, continue without saving?",
      "Attention ! GD-Engine utilise une sauvegarde automatique.\nImpossible de sauvegarder les fichiers en sécurité, continuer sans sauvegarder ?",
      "Achtung! GD-Engine speichert automatisch.\nDateien können nicht sicher gespeichert werden. Ohne Speichern fortfahren?",
      "Attenzione! GD-Engine usa il salvataggio automatico.\nImpossibile salvare i file in sicurezza, continuare senza salvare?",
      "¡Atención! GD-Engine usa el guardado automático.\nNo se pueden guardar los archivos de forma segura, ¿continuar sin guardar?"}},
    {"save_saving",
     {"Saving data... Do not shut down or reset system.", "Sauvegarde... N'éteins pas et ne redémarre pas le système.",
      "Daten werden gespeichert... System nicht ausschalten oder neu starten.", "Salvataggio... Non spegnere né riavviare il sistema.",
      "Guardando datos... No apagues ni reinicies el sistema."}},
    {"mc_save_confirm",
     {"WARNING: Saving will overwrite any existing GD-Engine save data. Are you sure you want to save?",
      "ATTENTION : la sauvegarde remplacera les données GD-Engine existantes. Sauvegarder quand même ?",
      "WARNUNG: Beim Speichern werden vorhandene GD-Engine-Daten überschrieben. Wirklich speichern?",
      "ATTENZIONE: il salvataggio sovrascriverà i dati GD-Engine esistenti. Vuoi davvero salvare?",
      "ADVERTENCIA: al guardar se sobrescribirán los datos de GD-Engine existentes. ¿Seguro que quieres guardar?"}},
    {"mc_load_confirm",
     {"WARNING: If you load data, you will lose your current game progress. Continue with load?",
      "ATTENTION : si tu charges des données, tu perdras ta progression actuelle. Charger quand même ?",
      "WARNUNG: Beim Laden geht dein aktueller Spielfortschritt verloren. Trotzdem laden?",
      "ATTENZIONE: caricando i dati perderai i progressi attuali. Continuare?",
      "ADVERTENCIA: si cargas datos, perderás tu progreso actual. ¿Continuar?"}},
    {"pre_delete_confirm",
     {"Before deleting a band, your current progress must be saved. Any existing Guitar Hero data may be overwritten.",
      "Avant de supprimer un groupe, ta progression doit être sauvegardée. Les données Guitar Hero existantes peuvent être remplacées.",
      "Vor dem Löschen einer Band muss dein Fortschritt gespeichert werden. Vorhandene Guitar Hero-Daten können überschrieben werden.",
      "Prima di eliminare una band, i progressi devono essere salvati. I dati Guitar Hero esistenti potrebbero essere sovrascritti.",
      "Antes de borrar un grupo, debes guardar tu progreso. Los datos de Guitar Hero existentes pueden sobrescribirse."}},
    // A controller lost mid-song (CONTROLLER_LOSS): PCs have no numbered
    // controller ports (as GD01-191).
    {"controller_loss_msg", CTRL_LOSS},
    {"tut_guitar_loss_msg", CTRL_LOSS},
    {"controller_loss_msg_multi",
     {"Please make sure both players' Guitar Controllers are connected.",
      "Vérifie que les manettes guitare des deux joueurs sont bien branchées.",
      "Bitte stelle sicher, dass die Gitarren-Controller beider Spieler angeschlossen sind.",
      "Assicurati che le chitarre di entrambi i giocatori siano collegate.",
      "Asegúrate de que los mandos de guitarra de ambos jugadores están conectados."}},
    {"guitar_help_plugin",
     {"Plug in the Guitar Controller and attach the guitar strap.", "Branche la manette guitare et attache la sangle.",
      "Schließe den Gitarren-Controller an und befestige den Gurt.", "Collega la chitarra e aggancia la tracolla.",
      "Conecta el mando de guitarra y engancha la correa."}},
    // Only when saving fails on PC.
    {"save_error",
     {"SAVE ERROR: Data was not saved successfully. Retry or Continue without saving?",
      "ERREUR DE SAUVEGARDE : les données n'ont pas été sauvegardées. Réessayer ou continuer sans sauvegarder ?",
      "SPEICHERFEHLER: Die Daten wurden nicht gespeichert. Wiederholen oder ohne Speichern fortfahren?",
      "ERRORE DI SALVATAGGIO: i dati non sono stati salvati. Riprovare o continuare senza salvare?",
      "ERROR AL GUARDAR: no se han guardado los datos. ¿Reintentar o continuar sin guardar?"}},
    {"mc_nodata_error",
     {"No GD-Engine data found. Do you want to create and try loading again?",
      "Aucune donnée GD-Engine trouvée. Créer les données et réessayer le chargement ?",
      "Keine GD-Engine-Daten gefunden. Daten anlegen und erneut laden?",
      "Nessun dato GD-Engine trovato. Crearli e riprovare a caricare?",
      "No se han encontrado datos de GD-Engine. ¿Crearlos y volver a cargar?"}},
    {"mc_corrupt_error",
     {"LOAD FAILED: Corrupt data detected. Try loading again?", "ÉCHEC DU CHARGEMENT : données corrompues. Réessayer ?",
      "LADEN FEHLGESCHLAGEN: Beschädigte Daten. Erneut laden?", "CARICAMENTO NON RIUSCITO: dati danneggiati. Riprovare?",
      "ERROR DE CARGA: datos dañados. ¿Volver a intentarlo?"}},
    {"bootup_corrupt_data",
     {"LOAD FAILED: Corrupt data detected or no free space on disk. Try loading again?",
      "ÉCHEC DU CHARGEMENT : données corrompues ou disque plein. Réessayer ?",
      "LADEN FEHLGESCHLAGEN: Beschädigte Daten oder kein freier Speicherplatz. Erneut laden?",
      "CARICAMENTO NON RIUSCITO: dati danneggiati o disco pieno. Riprovare?",
      "ERROR DE CARGA: datos dañados o disco lleno. ¿Volver a intentarlo?"}},
    {"bootup_replace_save_check",
     {"SAVE ERROR: GD-Engine save data already exists and we are conflicting. Select REPLACE to create a brand new save file, or LOAD to use that save data.",
      "ERREUR DE SAUVEGARDE : une sauvegarde GD-Engine existe déjà. Choisis REMPLACER pour créer un nouveau fichier, ou CHARGER pour l'utiliser.",
      "SPEICHERFEHLER: Es gibt bereits GD-Engine-Speicherdaten. Wähle ERSETZEN für eine neue Speicherdatei oder LADEN, um sie zu verwenden.",
      "ERRORE DI SALVATAGGIO: esiste già un salvataggio GD-Engine. Seleziona SOSTITUISCI per crearne uno nuovo o CARICA per usarlo.",
      "ERROR AL GUARDAR: ya hay datos guardados de GD-Engine. Selecciona SUSTITUIR para crear un archivo nuevo o CARGAR para usarlos."}},
    {"file_exists_error",
     {"SAVE ERROR: GD-Engine data already exists. Overwrite previous data?",
      "ERREUR DE SAUVEGARDE : des données GD-Engine existent déjà. Les remplacer ?",
      "SPEICHERFEHLER: Es gibt bereits GD-Engine-Daten. Überschreiben?",
      "ERRORE DI SALVATAGGIO: i dati GD-Engine esistono già. Sovrascriverli?",
      "ERROR AL GUARDAR: ya existen datos de GD-Engine. ¿Sobrescribirlos?"}},
    // PS2 card states (cannot happen on PC).
    {"bootup_nocard",
     {"No data device detected in the system. GD-Engine uses an Autosave feature. Do you want to start the game without saving?",
      "Aucun périphérique de sauvegarde détecté. GD-Engine utilise une sauvegarde automatique. Lancer le jeu sans sauvegarder ?",
      "Kein Speichergerät gefunden. GD-Engine speichert automatisch. Spiel ohne Speichern starten?",
      "Nessun dispositivo di salvataggio rilevato. GD-Engine usa il salvataggio automatico. Avviare il gioco senza salvare?",
      "No se ha detectado ningún dispositivo de guardado. GD-Engine usa el guardado automático. ¿Empezar sin guardar?"}},
    {"bootup_nospace",
     {"Insufficient space. Provide more free space on the disk the game is running from. Continue without saving?",
      "Espace insuffisant. Libère de l'espace sur le disque d'où le jeu est lancé. Continuer sans sauvegarder ?",
      "Nicht genügend Speicherplatz. Schaffe Platz auf dem Laufwerk, von dem das Spiel läuft. Ohne Speichern fortfahren?",
      "Spazio insufficiente. Libera spazio sul disco da cui viene eseguito il gioco. Continuare senza salvare?",
      "Espacio insuficiente. Libera espacio en el disco desde el que se ejecuta el juego. ¿Continuar sin guardar?"}},
    {"bootup_create_save_no_space",
     {"Insufficient space. Provide more free space on the disk the game is running from. Continue without saving?",
      "Espace insuffisant. Libère de l'espace sur le disque d'où le jeu est lancé. Continuer sans sauvegarder ?",
      "Nicht genügend Speicherplatz. Schaffe Platz auf dem Laufwerk, von dem das Spiel läuft. Ohne Speichern fortfahren?",
      "Spazio insufficiente. Libera spazio sul disco da cui viene eseguito il gioco. Continuare senza salvare?",
      "Espacio insuficiente. Libera espacio en el disco desde el que se ejecuta el juego. ¿Continuar sin guardar?"}},
    {"different_card",
     {"The status of the saved data changed. Proceed with save?", "L'état des données sauvegardées a changé. Sauvegarder quand même ?",
      "Der Zustand der Speicherdaten hat sich geändert. Trotzdem speichern?", "Lo stato dei dati salvati è cambiato. Salvare comunque?",
      "El estado de los datos guardados ha cambiado. ¿Guardar de todos modos?"}},
    {"no_card_error",
     {"SAVE ERROR: No save device found. Do you wish to continue the game without saving?",
      "ERREUR DE SAUVEGARDE : aucun périphérique de sauvegarde. Continuer sans sauvegarder ?",
      "SPEICHERFEHLER: Kein Speichergerät gefunden. Ohne Speichern weiterspielen?",
      "ERRORE DI SALVATAGGIO: nessun dispositivo di salvataggio. Continuare senza salvare?",
      "ERROR AL GUARDAR: no hay dispositivo de guardado. ¿Continuar sin guardar?"}},
    {"no_space_error", ERR_SPACE},
    {"no_format_warn", ERR_SPACE},
    {"unformatted_card", ERR_SPACE},
    {"format_card", ERR_SPACE},
    {"format_card_error",
     {"SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Continue without saving?",
      "ERREUR DE SAUVEGARDE : espace insuffisant, format corrompu ou erreur inconnue. Continuer sans sauvegarder ?",
      "SPEICHERFEHLER: Nicht genügend Speicherplatz, beschädigtes Format oder unbekannter Fehler. Ohne Speichern fortfahren?",
      "ERRORE DI SALVATAGGIO: spazio insufficiente, formato danneggiato o errore sconosciuto. Continuare senza salvare?",
      "ERROR AL GUARDAR: espacio insuficiente, formato dañado o error desconocido. ¿Continuar sin guardar?"}},
    {"format_no_card_error", ERR_RETRY},
    {"already_formatted_error", ERR_RETRY},
    {"mc_card_removed_error", ERR_RETRY},
    {"mc_nocard_error", ERR_RETRY},
    {"bootup_caution_nodata", NO_DATA},
    {"bootup_save_card_removed", NO_DATA},
    {"bootup_space_check_failed",
     {"Caution! Insufficient space to create a save data. Continue without saving?",
      "Attention ! Espace insuffisant pour créer une sauvegarde. Continuer sans sauvegarder ?",
      "Achtung! Nicht genügend Speicherplatz für Speicherdaten. Ohne Speichern fortfahren?",
      "Attenzione! Spazio insufficiente per creare un salvataggio. Continuare senza salvare?",
      "¡Atención! Espacio insuficiente para crear datos guardados. ¿Continuar sin guardar?"}},
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
    {"gd_loading_tip5", {"GD-Engine Devs: The original game came out in 2007... This project is from 2026... 19 years... I'm old.."}},
    {"gd_loading_tip6",
     {"GD-Engine Devs: Thanks to my friends who I message non stop during each milestone of this project: Adler, Lucas, Colin, Mauricio, "
      "Geovane, Caio, Joao, Gabriel...\nYOU GUYS ROCK! (Don't block me)"}},
};

#undef ERR_SPACE
#undef ERR_RETRY
#undef NO_DATA
#undef CTRL_LOSS

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

void applyPcStrings(std::map<std::string, std::string>& strings, const std::string& language) {
    const int li = languageIndex(language);
    for (const PcString& s : kPcStrings) {
        const std::string text = s.text[li] ? s.text[li] : s.text[0];
        strings[s.token] = text;
        if (const std::string ps2 = std::string(s.token) + "_ps2"; strings.count(ps2)) strings[ps2] = text;
    }
}

}  // namespace scene
