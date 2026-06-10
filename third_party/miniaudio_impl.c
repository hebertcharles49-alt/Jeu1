/* miniaudio — l'unique unité de traduction portant l'implémentation. Surface
 * réduite à la LECTURE de device : on génère nos samples (ni décodage, ni
 * encodage, ni resource manager, ni node graph). Compilée à part : les
 * warnings de la lib n'entachent pas le code SCPS (toujours 0 warning). */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
