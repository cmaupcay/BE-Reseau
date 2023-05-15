# BE Réseaux

« _L’objectif du BE est de concevoir et de développer en langage C un __protocole de niveau Transport__ appelé __MICTCP__ visant à transporter un __flux vidéo__ distribué en temps réel._ »

- [Recettes](#recettes)
  - [Test](#test)
    - [Changement de vidéo](#changement-de-vidéo)
  - [Debug](#debug)
- [Applications](#applications)
  - [tsock\_test](#tsock_test)
  - [tsock\_texte \& tsock\_video](#tsock_texte--tsock_video)

## Recettes

Pour compiler le projet, utilisez les commandes ```make``` ou ```make all```.
Pour nettoyer le dossier, utilisez ```make clean```.

### Test

Pour tester le protocole, utilisez la commande ```make test```, qui utilise le script ___[tsock_test](#tsock_test)___.

#### Changement de vidéo

Pour changer la vidéo par défaut (_```video/video.bin```_), vous pouvez utiliser les commandes ```make video.starwars``` et ```make video.wildlife```.

<!-- ### PDF -->

<!-- Pour générer le PDF d'explication, utilisez la recette ```make pdf```. -->

### Debug

Pour modifier le niveau de débogage du protocole, vous pouvez utiliser les recettes suivantes :

| Commande                     | Description                                    |
| ---------------------------- | ---------------------------------------------- |
| ```make debug```             | _Activation de tous les éléments de débogage._ |
| ```make debug.functions```   | _Affichage des appels de fonctions._           |
| ```make debug.reliability``` | _Affichage des statistiques de fiabilité._     |

## Applications

Plusieurs applications permettent de tester le protocole ___MICTCP___.

### tsock_test

Le script __```tsock_test```__ lance des tests basés sur __```tsock_video```__ et peut changer la vidéo cible dynamiquement. Le protocole est recompilé à chaque étape en augmentant la __fréquence de perte__ progressivement, et plusieurs éléments de débogage sont activés.

### tsock_texte & tsock_video

« _Deux applications de test sont fournies, __tsock_texte__ et __tsock_video__, elles peuvent être lancées soit en __mode puits__, soit en __mode source__ selon la syntaxe suivante:_ »

```shell
Usage: ./tsock_texte [-p|-s]
Usage: ./tsock_video [[-p|-s] [-t (tcp|mictcp)]
```
