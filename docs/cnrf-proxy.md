# Proxy CNRF Asgard — ajout au projet Gekkekoe

État du **11/10/2026** : extension expérimentale de `components/ecodan`, issue de l’amont `7672b7f`. Le mode d’émulation original reste disponible. Les tests logiciels ne valident pas le raccordement électrique ni les délais acceptés par Mitsubishi.

## Configuration et démarrage

Utiliser `ecodan-cnrf-proxy-asgard.yaml` avec **ESPHome 2026.9.1** (`requirements-dev.txt`). Le fichier charge les composants du clone local ; il ne télécharge pas une version amont qui remplacerait les modifications.

```text
FTC CNRF → Asgard HP → Asgard SL → PAR-WR61R-E ↔ PAR-WT60R-E
           RX38/TX39   RX8/TX7
```

Les deux UART utilisent 2400 bauds, 8 bits, parité paire, un stop. Vérifier les références réellement inscrites sur les équipements. Le proxy occupe les deux ports ; une supervision CN105 simultanée nécessite une autre interface.

Créer `secrets.yaml` à partir de `secrets.example.yaml` seulement si le fichier local n’existe pas. Renseigner `wifi_ssid`, `wifi_password`, `api_encryption_key` (32 octets en base64), `web_server_username` et `web_server_password`. `secrets.yaml` et `.esphome/` sont exclus de Git. Dans ce workspace, les cinq clés ont été reprises du fichier ESPHome fourni par l’utilisateur, sans publier leurs valeurs.

```sh
esphome config ecodan-cnrf-proxy-asgard.yaml
esphome compile ecodan-cnrf-proxy-asgard.yaml
```

Ces commandes ne téléversent rien. L’exemple utilise des partitions de 4 Mo : identifier la capacité de flash avant un éventuel téléversement. Le firmware compilé contient les identifiants locaux ; ne pas publier son binaire.

Le pont démarre avant le Wi-Fi. L’override est désactivé après chaque démarrage. Une perte du Wi-Fi ou de Home Assistant ne provoque pas de redémarrage automatique du proxy.

## Interface web et Home Assistant

Ouvrir `http://ecodan-cnrf-proxy.local/` (ou son IP). La page ESPHome est embarquée, accessible sans CDN et protégée par les identifiants web avec authentification Digest.

- Saisir la **température manuelle à appliquer**, entre 10 et 30 °C. La saisie seule n’active rien.
- **Appliquer la température** déclare cette valeur comme mesure ponctuelle fraîche pour la **durée maximale de la mesure** affichée (10 minutes par défaut). La résolution CNRF est de 0,5 °C.
- **Revenir au thermostat** désactive la modification des prochaines trames.
- Les températures, l’expiration, les compteurs et l’âge des derniers octets sont actualisés toutes les deux secondes. Ces lectures ne renouvellent pas la mesure.

« Remplacement actif » signifie qu’il est autorisé sur une prochaine trame valide contenant RC1. « Dernière température préparée pour la FTC » correspond à la sortie du parseur, avant la file d’émission ; ce n’est pas un acquittement de la PAC. Aucune donnée reçue est affichée comme inconnue, et les âges permettent de repérer une liaison silencieuse. La dernière température observée ne prouve pas à elle seule que la communication continue.

L’API native ESPHome chiffrée expose deux actions :

| Action | Paramètres | Effet |
|---|---|---|
| `cnrf_override` | `temperature: float`, `sample_age_ms: int` | Validation atomique et activation pour `proxy_override_timeout - sample_age_ms` ms. |
| `cnrf_disable` | Aucun | Désactivation, sans émission CNRF spontanée. |

Une température non finie/hors plage, un âge négatif ou supérieur ou égal à `proxy_override_timeout` et un pont indisponible font échouer l’activation sans rafraîchir l’état précédent. La réponse native indique le succès ou l’échec ; si les données de réponse sont demandées, elle contient aussi `remaining_ms`.

Avec le nom de nœud fourni, les actions HA sont normalement `esphome.ecodan_cnrf_proxy_cnrf_override` et `esphome.ecodan_cnrf_proxy_cnrf_disable` : vérifier leur nom dans les outils de développement après ajout de l’intégration ESPHome. L’exemple [home_assistant_package.example.yaml](../home_assistant_package.example.yaml) garde les noms de scripts `cnrf_proxy_temperature_chambre` et `cnrf_proxy_retour_salon`.

La plage exemple est **22 h 30–7 h**, avec déclencheurs aux bornes, au démarrage HA, aux changements de mesure/horodatage et toutes les minutes. Remplacer les deux identifiants `sensor.temperature_chambre` et `sensor.temperature_chambre_last_seen`. Le second est un placeholder : il doit dater la mesure physique correspondant à la valeur du premier. Une température stable peut être fraîche. `last_changed`, `now()` et le simple renvoi d’un cache ne prouvent pas une nouvelle mesure. Les états marqués `restored`, horodatages absents/futurs et données périmées sont refusés par l’exemple.

Adapter aussi `sensor.ecodan_cnrf_proxy_override_timeout` à l’entité ESPHome **Durée maximale de la mesure**, en secondes, ou renommer cette entité dans HA. Ce délai provient de `proxy_override_timeout` : il n’est plus recopié dans l’automatisation. Son changement déclenche une réévaluation ; s’il est absent, invalide ou restauré, l’exemple demande le retour au thermostat. Le bouton web renommé peut nécessiter l’adaptation d’une automatisation qui utilisait son ancien identifiant.

L’âge est déclaré par le client : le firmware ne peut pas détecter un capteur ancien présenté à tort comme neuf. L’automatisation et les boutons web utilisent le même état ; la dernière commande valide l’emporte. Pour un essai manuel, suspendre l’automatisation HA concernée.

Le serveur HTTP Arduino `/api/override` n’est pas repris dans cette extension. Les appels HA utilisent les actions natives atomiques, plutôt qu’une succession de réglages HTTP de température et d’âge. Les routes REST standards d’ESPHome restent disponibles pour les entités web.

## Garanties du mode proxy et limites

Configurer `proxy_mode: true`, deux UART distinctes et `proxy_controller: 1` (RC1 ; valeurs 1 à 8 admises). `proxy_override_timeout` fixe la durée maximale d’une mesure ; sa valeur par défaut est `10min`. Le validateur refuse un débit/parité incorrects, une UART manquante, des broches inversées et le débogage UART partagé. Les UART doivent être réservées au proxy : ne pas appeler leurs fonctions depuis une autre lambda.

Le mode strict court-circuite les boucles d’émulation, le changement automatique de débit, les commandes de connexion et les setters du thermostat simulé. Il transmet les octets FTC vers récepteur sans interprétation. Dans l’autre sens, seule une trame complète `FC 48 04 03 10 27 … checksum`, avec checksum valide et RC présent, peut être modifiée. `0xFF` ne devient jamais une sonde artificielle. Les trames inconnues, corrompues ou tronquées sont restituées ; les longueurs déclarées empêchent de reconnaître une fausse trame dans un autre payload.

Une tâche FreeRTOS possède les pilotes UART IDF. Chaque UART a un tampon RX de 512 octets et une file logicielle TX de 512 octets, servie par écritures non bloquantes et lots bornés. Le seuil RX matériel est fixé à **un octet** pour transmettre progressivement. La file de 544 événements par UART permet de garder les erreurs même après une accumulation d’événements de réception ; elle est parcourue avant de traiter les données. Les pilotes sont réinstallés après l’initialisation ESPHome pour recevoir ces événements ; les autres utilisateurs de ces UART sont interdits. Une erreur de parité, de trame ou un débordement invalide la candidate et désactive l’override jusqu’à une nouvelle activation explicite. Une perte physique d’octets ne peut pas être réparée.

En mode transparent, le parseur ne retient pas les octets. En mode override, il attend le checksum de la candidate : **environ 101 ms d’attente pour 22 caractères à 2400 bauds/8E1**, auxquels s’ajoutent l’ordonnancement et la file d’émission. La tâche libère une candidate incomplète après 20 ms sans octet traité, seulement si la FIFO matérielle et le tampon RX sont vides. Cette vérification se fait sous section critique sur le cœur des interruptions UART ; un retard de traitement d’octets déjà reçus ne crée plus une frontière de trame. En cas de retard masquant un vrai silence, la resynchronisation peut attendre le prochain silence observable. Ces délais doivent être vérifiés sur les équipements réels avant exploitation.

À expiration, les prochaines trames officielles repassent. Le logiciel ne crée pas de mesure ni de message pour masquer l’absence du récepteur. Une panne ou une coupure d’alimentation du proxy peut interrompre CNRF : aucun bypass matériel n’est démontré.

## Vérification et recette restante

Depuis le clone, dans l’environnement Python d’ESPHome :

```sh
sh tests/run_native.sh
python tests/test_config.py
python tests/test_ha_templates.py
```

Les tests natifs utilisent AddressSanitizer et UndefinedBehaviorSanitizer. Ils couvrent le checksum, RC1/RC2, la plage et l’arrondi, une sonde absente, les entêtes inconnues, les troncatures, l’expiration, le débordement de `millis()`, les files et les écritures partielles. La tâche de production est aussi exécutée contre un simulateur des pilotes IDF : **1 000 trames dans chaque sens**, TX bloqué, erreurs UART, reprise explicite et absence de rafraîchissement sur entrée invalide. Cela ne simule ni les fronts électriques ni la charge réseau réelle.

Sept régressions supplémentaires exécutent la tâche de production avec réception en FIFO, livraison retardée, dix trames consécutives, vrai silence à 20 ms, erreur UART derrière 500 événements et activation juste avant le verrou dans les deux chemins concernés. Les tests de templates exercent dix-sept cas, dont valeur stable fraîche, horodatage absent/futur, cache restauré, passage à 7 h et délais de cinq/quinze minutes. La validation dans l’instance Home Assistant réelle reste à faire.

Avant raccordement : contrôle de continuité et de l’orientation des câbles, passage du 12 V, niveaux RX/TX et fronts sous charge. Le manuel Asgard interdit l’alimentation USB simultanée à l’alimentation PAC. Mesurer d’abord une liaison directe puis le proxy transparent ; vérifier ensuite la latence des candidates actives avec le récepteur réel. Tester HTTP lent, Wi-Fi coupé/rétabli, récepteur absent, expiration, redémarrage, commandes de consigne et ECS. Si la latence provoque des timeouts, garder l’override désactivé et reprendre l’analyse des captures.

Voir également le [dossier électrique du workspace](../../Asguard/Analyse_Electronique_Asgard_v1.md) et le [plan de recette](../../docs/superpowers/plans/2026-10-09_Proxy_CNRF_Interface_Web.md). Ces liens concernent l’organisation locale du workspace.
