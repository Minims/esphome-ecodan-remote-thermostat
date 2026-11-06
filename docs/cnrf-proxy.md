# Proxy CNRF Asgard — ajout au projet Gekkekoe

État du **11/10/2026** : extension expérimentale de `components/ecodan`, issue de l’amont `7672b7f`. Le mode d’émulation original reste disponible. Les tests logiciels ne valident pas le raccordement électrique ni les délais acceptés par Mitsubishi.

## Configuration et démarrage

Utiliser `ecodan-cnrf-proxy-asgard.yaml` avec **ESPHome 2026.10.2** (`requirements-dev.txt`). Le fichier charge les composants du clone local ; il ne télécharge pas une version amont qui remplacerait les modifications.

```text
FTC CNRF → Asgard HP → Asgard SL → PAR-WR61R-E ↔ PAR-WT60R-E
           RX38/TX39   RX8/TX7
```

Les deux UART utilisent 2400 bauds, 8 bits, parité paire, un stop. Vérifier les références réellement inscrites sur les équipements. Le proxy occupe les deux ports ; une supervision CN105 simultanée nécessite une autre interface.

Créer `secrets.yaml` à partir de `secrets.example.yaml` seulement si le fichier local n’existe pas. Renseigner `wifi_ssid`, `wifi_password`, `api_encryption_key` (32 octets en base64), `ota_password`, `web_server_username` et `web_server_password`. Pour mettre à jour un appareil existant, conserver ses identifiants Wi-Fi, API et OTA. `secrets.yaml` et `.esphome/` sont exclus de Git.

```sh
esphome config ecodan-cnrf-proxy-asgard.yaml
esphome compile ecodan-cnrf-proxy-asgard.yaml
```

Ces commandes ne téléversent rien. L’exemple utilise des partitions de 4 Mo : identifier la capacité de flash avant un éventuel téléversement. Le firmware compilé contient les identifiants locaux ; ne pas publier son binaire.

Le firmware conserve les mises à jour OTA natives avec mot de passe et depuis la page web authentifiée. Pour téléverser le binaire compilé vers le proxy identifié : `esphome upload ecodan-cnrf-proxy-asgard.yaml --device <adresse-du-proxy>`.

Le pont démarre avant le Wi-Fi. L’override est désactivé après chaque démarrage. Une perte du Wi-Fi ou de Home Assistant ne provoque pas de redémarrage automatique du proxy.

## Interface web et Home Assistant

Ouvrir `http://ecodan-cnrf-proxy.local/` (ou son IP). La page ESPHome est embarquée, accessible sans CDN et protégée par les identifiants web avec authentification Digest.

- Saisir la **température manuelle à appliquer**, entre 10 et 30 °C. La saisie seule n’active rien.
- **Durée maximale de la mesure** est un réglage modifiable sur le web et dans Home Assistant, en secondes, de 1 à 2 073 600 (24 jours). Sa valeur initiale est 600 s (10 minutes) et ESPHome la mémorise pour les redémarrages suivants.
- **Appliquer la température** déclare cette valeur comme mesure ponctuelle fraîche pour la **durée maximale de la mesure** affichée (10 minutes par défaut). La résolution CNRF est de 0,5 °C.
- **Revenir au thermostat** désactive la modification des prochaines trames.
- Les températures, l’expiration, les compteurs et l’âge des derniers octets sont actualisés toutes les deux secondes. Ces lectures ne renouvellent pas la mesure.

Changer la durée annule le remplacement en cours sans rajeunir la mesure. Le blueprint réévalue alors la sonde avec la nouvelle limite ; en usage manuel, réappuyer sur **Appliquer la température**. Renvoyer la même durée ne désactive pas le remplacement.

« Remplacement actif » signifie qu’il est autorisé sur une prochaine trame valide contenant RC1. « Dernière température préparée pour la FTC » correspond à la sortie du parseur, avant la file d’émission ; ce n’est pas un acquittement de la PAC. Aucune donnée reçue est affichée comme inconnue, et les âges permettent de repérer une liaison silencieuse. La dernière température observée ne prouve pas à elle seule que la communication continue.

L’API native ESPHome chiffrée expose les actions suivantes :

| Action | Paramètres | Effet |
|---|---|---|
| `cnrf_override` | `temperature: float`, `sample_age_ms: int` | Validation atomique et activation pour la durée actuellement réglée moins `sample_age_ms`, en ms. |
| `cnrf_disable` | Aucun | Désactivation, sans émission CNRF spontanée. |
| `cnrf_capture_start` | Aucun | Démarre une capture RX de 120 secondes dans les deux sens et efface la capture précédente. |
| `cnrf_capture_read` | Aucun | Renvoie et vide les tampons de capture, sans prolonger sa durée ni modifier le remplacement. |

La capture est désactivée au démarrage. Appeler `cnrf_capture_start`, puis
`cnrf_capture_read` avec les données de réponse demandées, environ deux fois par
seconde. La réponse contient `active` et deux objets, `ftc_rx` (reçu de la PAC) et
`receiver_rx` (reçu du récepteur, avant remplacement). Chaque objet contient
`hex`, les octets bruts en hexadécimal, `first_ms` et `last_ms`, leurs instants de
lecture depuis le démarrage de l’ESP, et `dropped`, le nombre d’octets non capturés
depuis la dernière lecture. Les blocs peuvent couper une trame : concaténer les
blocs d’un même sens pour l’analyser. Ces données RX ne prouvent pas la réception
des octets transmis par l’autre appareil.

Chaque sens dispose de 512 octets. Si le lecteur tarde ou se déconnecte, les
octets excédentaires ne sont pas capturés ; la transmission UART continue.
Un `dropped` non nul signale une coupure dans la capture après les octets du bloc.
Après 120 secondes, la copie s’arrête ; les derniers octets restent lisibles.
Le formatage et la réponse réseau s’exécutent hors de la tâche UART.

Une température non finie/hors plage, un âge négatif ou supérieur ou égal à la durée actuellement réglée (en ms) et un pont indisponible font échouer l’activation sans rafraîchir l’état précédent. La réponse native indique le succès ou l’échec ; si les données de réponse sont demandées, elle contient aussi `remaining_ms`.

Une mesure acceptée conserve la réécriture en cours si le remplacement précédent est encore valide et si la température arrondie à 0,5 °C reste identique. Sa validité est recalculée avec l’âge fourni, sans rajeunir la mesure. Si la température change ou si l’ancienne mesure a expiré, la trame en cours retrouve sa mesure d’origine ; la nouvelle mesure s’applique aux trames suivantes.

Avec le nom de nœud fourni, les actions HA sont normalement `esphome.ecodan_cnrf_proxy_cnrf_override` et `esphome.ecodan_cnrf_proxy_cnrf_disable` : vérifier leur nom dans les outils de développement après ajout de l’intégration ESPHome. L’exemple [home_assistant_package.example.yaml](../home_assistant_package.example.yaml) garde les noms de scripts `cnrf_proxy_temperature_chambre` et `cnrf_proxy_retour_salon`.

### Blueprint Home Assistant

L’option **Actif 24 h/24**, désactivée par défaut, ignore les heures de début et de fin. L’activer pour un remplacement permanent sans coupure de 23:59 à minuit. Les contrôles de température, d’état restauré et de fraîcheur restent appliqués ; le délai continue de provenir du réglage ESPHome.

Cliquer sur le bouton pour ouvrir l'import du blueprint dans votre instance :

[![Importer dans My Home Assistant](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2FMinims%2Fesphome-ecodan-remote-thermostat%2Fblob%2Fcnrf-proxy-asgard%2Fblueprints%2Fautomation%2Fecodan%2Fcnrf_proxy.yaml)

L'import utilise le fichier publié sur la branche `cnrf-proxy-asgard` du dépôt Minims.
Voir le [fonctionnement du bouton My Home Assistant](https://contributors.home-assistant.io/marketplace/guides/blueprints/).

Pour une installation manuelle, copier [cnrf_proxy.yaml](../blueprints/automation/ecodan/cnrf_proxy.yaml) dans
`/config/blueprints/automation/ecodan/cnrf_proxy.yaml` de Home Assistant (2024.10.0 minimum).
Recharger les blueprints depuis **Paramètres → Automatisations et scènes → Blueprints**,
puis créer une automatisation à partir de **PAC - Sonde externe via proxy CNRF**.
Sélectionner la sonde en °C et l'entité
ESPHome de domaine `number`, **Durée maximale de la mesure**, en secondes. Adapter les deux noms d'actions
ESPHome si le nœud a été renommé. Aucun script du package n'est nécessaire.
La date est lue directement dans `last_updated` de la sonde ; aucun capteur de date
séparé n'est demandé. Après réimport d'une ancienne version du blueprint,
reconfigurer l'automatisation existante pour retirer l'ancienne entrée `timestamp_sensor`.
Après passage du capteur de durée au réglage modifiable, installer le nouveau firmware,
réimporter le blueprint, puis sélectionner la nouvelle entité `number` dans l'automatisation.
L'ancien capteur `sensor` n'est plus utilisé.

La plage par défaut est **22 h 30–7 h**, modifiable dans le formulaire ; une plage
de jour fonctionne aussi et des heures identiques désactivent le remplacement.
Le blueprint réévalue la mesure aux bornes de la plage, au démarrage HA, aux
changements de la sonde ou du réglage de durée et chaque minute. Hors plage ou si les données
sont invalides, futures, restaurées ou périmées, il demande le retour au thermostat.
Il transmet l'âge de `last_updated` au firmware sans le remettre à zéro.
Cette date change lorsque la valeur ou un attribut change, contrairement à
`last_changed` qui suit seulement la valeur. Une température et des attributs
inchangés peuvent donc expirer malgré de nouveaux relevés. À l'inverse, un changement
d'attribut ou une republication de cache peut rajeunir cette date sans nouvelle mesure
physique : le contrôle de fraîcheur dépend du comportement de l'intégration de la sonde.
Voir les [dates des états Home Assistant](https://www.home-assistant.io/docs/configuration/state_object/).
Désactiver l'ancienne automatisation du package avant d'activer celle du blueprint,
et n'utiliser qu'une automatisation pour un même proxy afin d'éviter des commandes concurrentes.
Si HA est arrêté ou le proxy inaccessible, l'expiration locale du firmware reste applicable.

### Ancien exemple sous forme de package

La plage exemple est **22 h 30–7 h**, avec déclencheurs aux bornes, au démarrage HA, aux changements de mesure/horodatage et toutes les minutes. Remplacer les deux identifiants `sensor.temperature_chambre` et `sensor.temperature_chambre_last_seen`. Le second est un placeholder : il doit dater la mesure physique correspondant à la valeur du premier. Une température stable peut être fraîche. `last_changed`, `now()` et le simple renvoi d’un cache ne prouvent pas une nouvelle mesure. Les états marqués `restored`, horodatages absents/futurs et données périmées sont refusés par l’exemple.

Adapter aussi `number.ecodan_cnrf_proxy_override_timeout` au réglage ESPHome **Durée maximale de la mesure**, en secondes, ou renommer cette entité dans HA. Ce délai est lu depuis le réglage courant : il n’est pas recopié dans l’automatisation. Son changement déclenche une réévaluation ; s’il est absent, invalide ou restauré, l’exemple demande le retour au thermostat. Le bouton web renommé peut nécessiter l’adaptation d’une automatisation qui utilisait son ancien identifiant.

L’âge est déclaré par le client : le firmware ne peut pas détecter un capteur ancien présenté à tort comme neuf. L’automatisation et les boutons web utilisent le même état ; la dernière commande valide l’emporte. Pour un essai manuel, suspendre l’automatisation HA concernée.

Le serveur HTTP Arduino `/api/override` n’est pas repris dans cette extension. Les appels HA utilisent les actions natives atomiques, plutôt qu’une succession de réglages HTTP de température et d’âge. Les routes REST standards d’ESPHome restent disponibles pour les entités web.

## Garanties du mode proxy et limites

Configurer `proxy_mode: true`, deux UART distinctes et `proxy_controller: 1` (RC1 ; valeurs 1 à 8 admises). L'option du composant `proxy_override_timeout` reste disponible avec une valeur par défaut de `10min`. Dans le YAML fourni, le réglage `number` la remplace au démarrage par sa valeur mémorisée, ou par son `initial_value: 600` à la première utilisation. Modifier cette valeur initiale pour changer le défaut de l'interface. Le validateur refuse un débit/parité incorrects, une UART manquante, des broches inversées et le débogage UART partagé. Les UART doivent être réservées au proxy : ne pas appeler leurs fonctions depuis une autre lambda.

Le mode strict court-circuite les boucles d’émulation, le changement automatique de débit, les commandes de connexion et les setters du thermostat simulé. Il transmet les octets FTC vers récepteur sans interprétation. Dans l’autre sens, les trames complètes de mesure `FC 48 04 03 10 27 … checksum` et de consigne `FC 49 04 03 10 28 … checksum`, avec checksum valide et RC présent, peuvent être modifiées. Dans une trame de consigne, seule la mesure ambiante du RC sélectionné est remplacée ; la consigne reste inchangée. La position de cette mesure est conservée pour pouvoir la restaurer si le remplacement est annulé avant émission. `0xFF` ne devient jamais une sonde artificielle. Les trames inconnues, corrompues ou tronquées sont restituées ; les longueurs déclarées empêchent de reconnaître une fausse trame dans un autre payload.

Une tâche FreeRTOS possède les pilotes UART IDF. Chaque UART a un tampon RX de 512 octets et une file logicielle TX de 512 octets, servie par écritures non bloquantes et lots bornés. Une trame modifiée attend séparément (22 octets au maximum), après les octets précédents ; sa présence suspend la lecture du récepteur pour préserver l’ordre. Le seuil RX matériel est fixé à **un octet** pour transmettre progressivement. La file de 544 événements par UART permet de garder les erreurs même après une accumulation d’événements de réception ; elle est parcourue avant de traiter les données. Les pilotes sont réinstallés après l’initialisation ESPHome pour recevoir ces événements ; les autres utilisateurs de ces UART sont interdits. Une erreur de parité, de trame ou un débordement invalide la candidate et désactive l’override jusqu’à une nouvelle activation explicite. Une perte physique d’octets ne peut pas être réparée.

En mode transparent, le parseur ne retient pas les octets. En mode override, il attend le checksum de la candidate : **environ 101 ms d’attente pour 22 caractères à 2400 bauds/8E1**, auxquels s’ajoutent l’ordonnancement et la file d’émission. La tâche libère une candidate incomplète après 20 ms sans octet traité, seulement si la FIFO matérielle et le tampon RX sont vides. Cette vérification se fait sous section critique sur le cœur des interruptions UART ; un retard de traitement d’octets déjà reçus ne crée plus une frontière de trame. En cas de retard masquant un vrai silence, la resynchronisation peut attendre le prochain silence observable. Ces délais doivent être vérifiés sur les équipements réels avant exploitation.

Avant le premier octet écrit dans la FIFO TX, une trame modifiée en attente retrouve sa température et son checksum d’origine si la mesure a expiré, a été désactivée ou remplacée par une température différente, si la durée a changé ou si une erreur UART est survenue. Une trame dont l’émission a commencé est terminée sans modification supplémentaire pour préserver son checksum. Les compteurs et la dernière température préparée décrivent toujours la sortie du parseur, même si le remplacement est ensuite annulé avant émission. À expiration, les prochaines trames officielles repassent. Le logiciel ne crée pas de mesure ni de message pour masquer l’absence du récepteur. Une panne ou une coupure d’alimentation du proxy peut interrompre CNRF : aucun bypass matériel n’est démontré.

## Vérification et recette restante

Depuis le clone, dans l’environnement Python d’ESPHome :

```sh
sh tests/run_native.sh
python tests/test_config.py
python tests/test_ha_templates.py
python tests/test_ha_blueprint.py
```

Les tests natifs utilisent AddressSanitizer et UndefinedBehaviorSanitizer. Ils couvrent le checksum, RC1/RC2, la plage et l’arrondi, une sonde absente, les entêtes inconnues, les troncatures, l’expiration, le débordement de `millis()`, les files et les écritures partielles. La tâche de production est aussi exécutée contre un simulateur des pilotes IDF : **1 000 trames dans chaque sens**, TX bloqué, erreurs UART, reprise explicite et absence de rafraîchissement sur entrée invalide. Cela ne simule ni les fronts électriques ni la charge réseau réelle.

Quatorze régressions supplémentaires exécutent la tâche de production : réception en FIFO, livraison retardée, dix trames consécutives, vrai silence à 20 ms, erreur UART derrière 500 événements, activation juste avant le verrou, remplacement d’une mesure pendant une trame avant/après expiration et rejet d’une commande invalide. Elles couvrent aussi les erreurs de trame sur chaque UART et le refus de démarrage si leur interruption ne peut pas être activée. Les tests de templates exercent dix-sept cas, dont valeur stable fraîche, horodatage absent/futur, cache restauré, passage à 7 h et délais de cinq/quinze minutes. La validation dans l’instance Home Assistant réelle reste à faire.

Deux autres régressions couvrent le changement de durée à chaud (bornes, entrées invalides, annulation de l'ancienne mesure, expiration sous la nouvelle limite) et son changement pendant une trame. Le blueprint est exercé sur 37 scénarios de templates, avec la durée lue depuis une entité `number`.

Sept régressions couvrent aussi les trames en attente TX : expiration, désactivation, remplacement de mesure, changement de durée, erreur UART, émission déjà commencée et conservation de l’ordre entre octets transparents et trames.

Les deux trames de consigne capturées sur le PAR-WR61R-E (16,5 puis 16 °C,
avec une mesure ambiante à 23 °C) servent de régressions : seule la mesure devient
18 °C, avec recalcul du checksum. Les tests couvrent aussi RC1/RC2/RC8, la sonde
absente, les troncatures et les trames invalides. Les scénarios de la tâche de
production sont exécutés pour les deux formats, notamment pour vérifier qu’une
annulation avant TX restaure la mesure sans altérer la consigne.

Deux régressions injectent une erreur de parité, sur chaque UART, entre la lecture initiale des événements et le premier octet TX. Les événements sont revérifiés sous section critique avant l'émission pour annuler le remplacement dans ce cas.

Avant raccordement : contrôle de continuité et de l’orientation des câbles, passage du 12 V, niveaux RX/TX et fronts sous charge. Le manuel Asgard interdit l’alimentation USB simultanée à l’alimentation PAC. Mesurer d’abord une liaison directe puis le proxy transparent ; vérifier ensuite la latence des candidates actives avec le récepteur réel. Tester HTTP lent, Wi-Fi coupé/rétabli, récepteur absent, expiration, redémarrage, commandes de consigne et ECS. Si la latence provoque des timeouts, garder l’override désactivé et reprendre l’analyse des captures.

Voir également le [dossier électrique du workspace](../../Asguard/Analyse_Electronique_Asgard_v1.md) et le [plan de recette](../../docs/superpowers/plans/2026-10-09_Proxy_CNRF_Interface_Web.md). Ces liens concernent l’organisation locale du workspace.

Dix régressions supplémentaires couvrent les renouvellements identiques en réception et avant TX, les valeurs donnant le même arrondi CNRF et les renouvellements après expiration, pour les trames de mesure et de consigne.
