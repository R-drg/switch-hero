#include "lang.hpp"
#include <atomic>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fret::lang {
namespace {
std::atomic<Language> language{Language::English};

// English -> Brazilian Portuguese. A "{}" in the English key is filled in by
// tr()'s extra arguments, in the same order in both languages.
const std::tuple<const char *, const char *, const char *> translationTable[] = {
    // English                              Portuguese                         Spanish
    {"looking for songs", "procurando músicas", "buscando canciones"},
    {"reading your sd card", "lendo seu cartão sd", "leyendo tu tarjeta SD"},
    {"found {} song", "{} música encontrada", "{} canción encontrada"},
    {"found {} songs", "{} músicas encontradas", "{} canciones encontradas"},
    {"burned mixtape vol. 1", "mixtape gravada vol. 1", "mixtape grabada vol. 1"},
    {"{} SONG", "{} MÚSICA", "{} CANCIÓN"},
    {"{} SONGS", "{} MÚSICAS", "{} CANCIONES"},
    {"QUICKPLAY", "JOGO RÁPIDO", "PARTIDA RÁPIDA"},
    {"DOWNLOAD SONGS", "BAIXAR MÚSICAS", "DESCARGAR CANCIONES"},
    {"OPTIONS", "OPÇÕES", "OPCIONES"},
    {"QUIT", "SAIR", "SALIR"},
    {"pick a song from your library", "escolha uma música da sua biblioteca", "elige una canción de tu biblioteca"},
    {"grab charts from chorus encore", "baixe charts do chorus encore", "descarga charts de Chorus Encore"},
    {"controls, calibration and gameplay", "controles, calibração e jogabilidade", "controles, calibración y jugabilidad"},
    {"back to the homebrew menu", "voltar ao menu homebrew", "volver al menú homebrew"},
    {"CHOOSE YOUR LANGUAGE", "ESCOLHA O IDIOMA", "ELIGE TU IDIOMA"},
    {"you can change it later in options",  "dá para mudar depois em opções", "puedes cambiarlo después en opciones"},

    // Hints
    {"SELECT", "ESCOLHER", "SELECCIONAR"},
    {"BACK", "VOLTAR", "VOLVER"},
    {"PLAY", "TOCAR", "REPRODUCIR"},
    {"CONFIRM", "CONFIRMAR", "CONFIRMAR"},
    {"CANCEL", "CANCELAR", "CANCELAR"},
    {"SELECT SONG", "ESCOLHER", "SELECCIONAR"},
    {"DOWNLOAD", "BAIXAR", "DESCARGAR"},
    {"DELETE", "APAGAR", "ELIMINAR"},
    {"SORT", "ORDENAR", "ORDENAR"},
    {"OPEN", "ABRIR", "ABRIR"},
    {"SAVE & BACK", "SALVAR E VOLTAR", "GUARDAR Y VOLVER"},
    {"CHANGE", "MUDAR", "CAMBIAR"},
    {"CLOSE", "FECHAR", "CERRAR"},
    {"SEARCH", "BUSCAR", "BUSCAR"},
    {"KEEP", "MANTER", "MANTENER"},
    {"RETRY", "REPETIR", "REINTENTAR"},
    {"CONTINUE", "CONTINUAR", "CONTINUAR"},
    {"RESUME", "RETOMAR", "REANUDAR"},

    // Parts and difficulties
    {"Guitar", "Guitarra", "Guitarra"},
    {"Bass", "Baixo", "Bajo"},
    {"Rhythm", "Base", "Ritmo"},
    {"Co-op", "Cooperativo", "Cooperativo"},
    {"Keys", "Teclado", "Teclado"},
    {"Easy", "Fácil", "Fácil"},
    {"Medium", "Médio", "Medio"},
    {"Hard", "Difícil", "Difícil"},

    // Song setup
    {"SELECT INSTRUMENT", "ESCOLHA O INSTRUMENTO", "SELECCIONA EL INSTRUMENTO"},
    {"SELECT DIFFICULTY", "ESCOLHA A DIFICULDADE", "SELECCIONA LA DIFICULTAD"},
    {"{} notes", "{} notas", "{} notas"},
    {"not charted", "sem chart", "sin chart"},
    {"NO FAIL", "SEM FALHA", "SIN FALLOS"},
    {"NO FAIL: ON", "SEM FALHA: SIM", "SIN FALLOS: SÍ"},
    {"NO FAIL: OFF", "SEM FALHA: NÃO", "SIN FALLOS: NO"},
    {"wii guitar mode", "modo guitarra de wii", "modo guitarra de Wii"},
    {"press-to-hit mode", "modo toque direto", "modo pulsación directa"},
    {"strum mode", "modo palhetada", "modo rasgueo"},
    {"BEST {}", "RECORDE {}", "RÉCORD {}"},

    // Song list
    {"pick a track off the mixtape", "escolha uma faixa da mixtape", "elige una canción de la mixtape"},
    {"by {}", "por {}", "por {}"},
    {"TITLE", "TÍTULO", "TÍTULO"},
    {"ARTIST", "ARTISTA", "ARTISTA"},
    {"STARS", "ESTRELAS", "ESTRELLAS"},
    {"NEW", "NOVAS", "NUEVAS"},
    {"NO SONGS FOUND", "NENHUMA MÚSICA", "NO SE ENCONTRARON CANCIONES"},
    {"Copy extracted Clone Hero song folders into", "Copie as pastas de músicas do Clone Hero, extraídas, para", "Copia las carpetas de canciones extraídas de Clone Hero en"},
    {"Each folder needs notes.chart or notes.mid plus audio",
    "Cada pasta precisa de notes.chart ou notes.mid e o áudio",
    "Cada carpeta necesita notes.chart o notes.mid y audio"},
    {"or press + to download charts", "ou aperte + para baixar charts", "o pulsa + para descargar charts"},
    {"UNSUPPORTED SONG", "MÚSICA NÃO SUPORTADA", "CANCIÓN NO COMPATIBLE"},
    {"reading chart", "lendo chart", "leyendo chart"},
    {"+ {} more", "+ {} outros", "+ {} más"},
    {"{} STEM", "{} FAIXA", "{} PISTA"},
    {"{} STEMS", "{} FAIXAS", "{} PISTAS"},
    {"chart warnings", "avisos no chart", "avisos del chart"},
    {"not played on {} {}", "ainda não jogada: {} {}", "aún no jugada en {} {}"},
    {"DELETE SONG?", "APAGAR MÚSICA?", "¿ELIMINAR CANCIÓN?"},
    {"Removes this folder and everything in it from the SD card.",
    "Remove esta pasta e tudo o que há nela do cartão SD.",
    "Elimina esta carpeta y todo lo que contiene de la tarjeta SD."},
    {"This can't be undone.", "Isso não pode ser desfeito.", "Esto no se puede deshacer."},
    {"KEEP IT", "MANTER", "CONSERVAR"},
    {"Deleted {}", "Apagada: {}", "Eliminada: {}"},

    // Options
    {"tune your rig", "ajuste seu equipamento", "ajusta tu equipo"},
    {"GAMEPLAY", "JOGABILIDADE", "JUGABILIDAD"},
    {"controller mode, no fail, note speed, lefty", "modo de controle, sem falha, velocidade, canhoto", "modo de control, sin fallos, velocidad de notas, zurdo"},
    {"AUDIO", "ÁUDIO", "AUDIO"},
    {"music and sound effect volume", "volume da música e dos efeitos", "volumen de la música y los efectos"},
    {"AUDIO / VIDEO SYNC", "SINCRONIA ÁUDIO / VÍDEO", "SINCRONIZACIÓN AUDIO / VÍDEO"},
    {"calibrate if notes feel early or late", "calibre se as notas parecerem fora do tempo", "calibra si las notas parecen adelantadas o retrasadas"},
    {"CONTROLS", "CONTROLES", "CONTROLES"},
    {"fret buttons and controller test", "botões dos trastes e teste do controle", "botones de los trastes y prueba del controlador"},
    {"LANGUAGE", "IDIOMA", "IDIOMA"},
    {"CUSTOMIZE", "PERSONALIZAR", "PERSONALIZAR"},
    {"highway and note colors", "pista e cores das notas", "pista y colores de las notas"},
    {"Highway", "Pista", "Pista"},
    {"Note colors", "Cores das notas", "Colores de las notas"},
    {"The board the notes run down. Frets, rails and the far end change with it.", "A pista por onde as notas descem. Trastes, bordas e o horizonte mudam junto.", "La pista por donde bajan las notas. Los trastes, los bordes y el fondo cambian con ella."},
    {"Gems, fret buttons, sustains and flames. Star power stays blue.", "Notas, botões dos trastes, sustains e chamas. O star power continua azul.", "Notas, botones de los trastes, sostenidos y llamas. El poder estrella se mantiene azul."},
    {"GRIP TAPE", "LIXA", "CINTA"},
    {"ROSEWOOD", "JACARANDÁ", "PALISANDRO"},
    {"SYNTHWAVE", "SYNTHWAVE", "SYNTHWAVE"},
    {"PASTEL DREAM", "SONHO PASTEL", "SUEÑO PASTEL"},
    {"CLASSIC", "CLÁSSICO", "CLÁSICO"},
    {"PASTEL", "PASTEL", "PASTEL"},
    {"NEON", "NEON", "NEÓN"},
    {"COLORBLIND", "DALTÔNICO", "DALTONISMO"},
    {"HELLFIRE", "FOGO DO INFERNO", "FUEGO DEL INFIERNO"},
    {"DIAMOND PLATE", "CHAPA XADREZ", "CHAPA LAGRIMADA"},
    {"CARBON FIBER", "FIBRA DE CARBONO", "FIBRA DE CARBONO"},
    {"THUNDERSTORM", "TEMPESTADE", "TORMENTA ELÉCTRICA"},
    {"TOXIC WASTE", "LIXO TÓXICO", "DESECHOS TÓXICOS"},
    {"ZEBRA", "ZEBRA", "CEBRA"},
    {"CHECKERBOARD", "XADREZ", "TABLERO DE AJEDREZ"},
    {"NEBULA", "NEBULOSA", "NEBULOSA"},
    {"FROSTBITE", "CONGELADO", "CONGELACIÓN"},
    {"BLOOD", "SANGUE", "SANGRE"},
    {"BONE", "OSSO", "HUESO"},
    {"OBSIDIAN", "OBSIDIANA", "OBSIDIANA"},
    {"GOLD RECORD", "DISCO DE OURO", "DISCO DE ORO"},
    {"PLATINUM", "PLATINA", "PLATINO"},
    {"RADIOACTIVE", "RADIOATIVO", "RADIACTIVO"},
    {"GHOST", "FANTASMA", "FANTASMA"},
    {"BUMBLEBEE", "ABELHA", "ABEJORRO"},
    {"CANDY CANE", "BENGALA DOCE", "CARAMELO"},
    {"VAPORWAVE", "VAPORWAVE", "VAPORWAVE"},
    {"ROYALTY", "REALEZA", "REALEZA"},
    {"CYBERPUNK", "CYBERPUNK", "CYBERPUNK"},
    {"INFERNO", "INFERNO", "INFIERNO"},
    {"GLACIER", "GELEIRA", "GLACIAR"},
    {"SUNSET", "PÔR DO SOL", "ATARDECER"},
    {"DEEP SEA", "MAR PROFUNDO", "MAR PROFUNDO"},
    {"RASTA", "RASTA", "RASTA"},
    {"ARCADE", "FLIPERAMA", "ARCADE"},
    {"CAMO", "CAMUFLADO", "CAMUFLADO"},
    {"SAKURA", "SAKURA", "SAKURA"},
    {"English or Portuguese", "inglês ou português", "inglés o portugués"},
    {"Options save when you leave this screen.", "As opções são salvas quando você sai desta tela.", "Las opciones se guardan al salir de esta pantalla."},
    {"Save and go back.", "Salvar e voltar.", "Guardar y volver."},
    {"ON", "LIGADO", "ACTIVADO"},
    {"OFF", "DESLIGADO", "DESACTIVADO"},
    {"TO START", "PARA COMEÇAR", "PARA EMPEZAR"},
    {"TO OPEN", "PARA ABRIR", "PARA ABRIR"},
    {"AGAIN TO CONFIRM", "DE NOVO PARA CONFIRMAR", "DE NUEVO PARA CONFIRMAR"},
    {"PRESS A BUTTON...", "APERTE UM BOTÃO...", "PULSA UN BOTÓN..."},
    {"Controller mode", "Modo de controle", "Modo de control"},
    {"WII GUITAR", "GUITARRA DE WII", "GUITARRA DE WII"},
    {"PRESS-TO-HIT", "TOQUE DIRETO", "PULSACIÓN DIRECTA"},
    {"STRUM", "PALHETADA", "RASGUEO"},
    {"Wii guitar over Bluetooth (needs the MissionControl patch). Fixed frets, strum bar strums.", "Guitarra de Wii por Bluetooth (requer o patch do MissionControl). Trastes fixos, palheta na barra.", "Guitarra de Wii por Bluetooth (requiere el parche de MissionControl). Trastes fijos, la barra de rasgueo funciona como rasgueo."},
    {"Press each fret as its note arrives. Best on Joy-Cons and the Pro Controller.", "Aperte cada traste quando a nota chegar. Melhor com Joy-Cons e o Pro Controller.", "Pulsa cada traste cuando llegue su nota. Funciona mejor con Joy-Cons y el Pro Controller."},
    {"Hold the frets and strum with the D-pad. Hammer-ons and taps need no strum.", "Segure os trastes e palhete com o direcional. Hammer-ons e taps não precisam de palhetada.", "Mantén pulsados los trastes y rasguea con la cruceta. Los hammer-ons y taps no necesitan rasgueo."},
    {"No fail", "Sem falha", "Sin fallos"},
    {"The rock meter can never fail you. Good for learning a song.", "O medidor de rock nunca te derruba. Bom para aprender uma música.", "El medidor de rock nunca te hará fallar. Ideal para aprender una canción."},
    {"Miss too much and the crowd boos you off stage.", "Erre demais e a plateia te vaia para fora do palco.", "Falla demasiado y el público te abucheará fuera del escenario."},
    {"Hit window", "Janela de acerto", "Ventana de acierto"},
    {"STRICT", "RIGOROSA", "ESTRICTA"},
    {"NORMAL", "NORMAL", "NORMAL"},
    {"LENIENT", "TOLERANTE", "PERMISIVA"},
    {"How far off a note can be and still count: +-{} ms on Expert, +-{} ms on Easy.", "Quanto uma nota pode desviar e ainda contar: +-{} ms no Expert, +-{} ms no Fácil.", "Cuánto puede desviarse una nota y seguir contando: +-{} ms en Experto, +-{} ms en Fácil."},
    {"Note speed", "Velocidade das notas", "Velocidad de las notas"},
    {"Faster notes spread out a busy chart. Each note is on screen for {} ms.", "Notas mais rápidas espalham um chart cheio. Cada nota fica {} ms na tela.", "Las notas más rápidas separan las notas de un chart cargado. Cada nota permanece en pantalla durante {} ms."},
    {"Lefty flip", "Modo canhoto", "Modo zurdo"},
    {"Mirrors the highway so green is on the right, for left-handed players.", "Espelha a pista para o verde ficar à direita, para quem é canhoto.", "Invierte la pista para que el verde quede a la derecha, para jugadores zurdos."},
    {"Music volume", "Volume da música", "Volumen de la música"},
    {"The song and its previews on the song list.", "A música e as prévias na lista de músicas.", "La canción y sus previsualizaciones en la lista de canciones."},
    {"Sound effects volume", "Volume dos efeitos", "Volumen de los efectos de sonido"},
    {"Menu sounds, the count-in, misses and star power.", "Sons do menu, contagem, erros e star power.", "Sonidos del menú, cuenta atrás, fallos y poder estrella."},
    {"Calibrate audio", "Calibrar áudio", "Calibrar audio"},
    {"Do this first. Tap along to a click track by ear to measure your audio delay.", "Faça isto primeiro. Toque junto com os cliques, de ouvido, para medir o atraso do áudio.", "Haz esto primero. Sigue los clics de oído para medir el retraso del audio."},
    {"Audio / input offset", "Atraso de áudio / entrada", "Desfase de audio / entrada"},
    {"Fine-tune by hand in 5 ms steps. Positive judges notes later.", "Ajuste fino em passos de 5 ms. Positivo avalia as notas mais tarde.", "Ajusta manualmente en pasos de 5 ms. Un valor positivo juzga las notas más tarde."},
    {"Calibrate video", "Calibrar vídeo", "Calibrar vídeo"},
    {"Tap as silent notes cross the line to line the highway up with the music.", "Toque quando as notas cruzarem a linha, sem som, para alinhar a pista com a música.", "Pulsa cuando las notas silenciosas crucen la línea para sincronizar la pista con la música."},
    {"Visual offset", "Atraso visual", "Desfase visual"},
    {"Fine-tune by hand in 5 ms steps. Positive draws notes later.", "Ajuste fino em passos de 5 ms. Positivo desenha as notas mais tarde.", "Ajusta manualmente en pasos de 5 ms. Un valor positivo dibuja las notas más tarde."},
    {"Timing overlay", "Medidor de desempenho", "Superposición de tiempos"},
    {"Shows frame times and audio clock drift in the corner. For checking smoothness.", "Mostra o tempo dos quadros e o desvio do áudio no canto. Para checar a fluidez.", "Muestra los tiempos de los fotogramas y el desfase del reloj de audio en la esquina. Para comprobar la fluidez."},
    {"Green fret", "Traste verde", "Traste verde"},
    {"Red fret", "Traste vermelho", "Traste rojo"},
    {"Yellow fret", "Traste amarelo", "Traste amarillo"},
    {"Blue fret", "Traste azul", "Traste azul"},
    {"Orange fret", "Traste laranja", "Traste naranja"},
    {"Fixed in Wii guitar mode.", "Fixo no modo guitarra de Wii.", "Fijo en el modo guitarra de Wii."},
    {"Press A, then the button or trigger to use.", "Aperte A e depois o botão ou gatilho que quer usar.", "Pulsa A y después el botón o gatillo que quieras usar."},
    {"Controller test", "Teste do controle", "Prueba del controlador"},
    {"Shows every button the game sees. Use it when a fret or guitar is not responding.", "Mostra cada botão que o jogo recebe. Use quando um traste ou a guitarra não responder.", "Muestra todos los botones que detecta el juego. Úsalo cuando un traste o la guitarra no respondan."},
    {"Reset all options", "Restaurar opções", "Restablecer todas las opciones"},
    {"Puts every option, offset and button back to its default.", "Volta todas as opções, atrasos e botões para o padrão.", "Restablece todas las opciones, desfases y botones a sus valores predeterminados."},
    {"Language", "Idioma", "Idioma"},
    {"Menus and messages. Song titles stay as they are.", "Menus e mensagens. Os nomes das músicas não mudam.", "Menús y mensajes. Los títulos de las canciones se mantienen sin cambios."},
    {"Press the button or trigger for this fret. Minus cancels.", "Aperte o botão ou gatilho para este traste. Menos cancela.", "Pulsa el botón o gatillo para este traste. Menos cancela."},
    {"Already assigned to another fret", "Já está em outro traste", "Ya está asignado a otro traste"},
    {"Options reset to defaults", "Opções restauradas para o padrão", "Opciones restablecidas a los valores predeterminados"},
    {"Controller mode reset to press-to-hit", "Modo de controle voltou para toque direto", "Modo de control restablecido a pulsación directa"},
    {"Controller disconnected", "Controle desconectado", "Controlador desconectado"},

    // Controller test
    {"CONTROLLER TEST", "TESTE DO CONTROLE", "PRUEBA DEL CONTROLADOR"},
    {"normal controller", "controle normal", "controlador normal"},
    {"player one (guitar)", "jogador um (guitarra)", "jugador uno (guitarra)"},
    {"guitar mode", "modo guitarra", "modo guitarra"},
    {"connected", "conectado", "conectado"},
    {"not connected", "desconectado", "desconectado"},
    {"on", "ligado", "encendido"},
    {"off", "desligado", "apagado"},
    {"styles (normal/player one)", "estilos (normal/jogador um)", "estilos (normal/jugador uno)"},
    {"raw buttons", "botões brutos", "botones sin procesar"},
    {"controller", "controle", "controlador"},
    {"no controller", "nenhum controle", "ningún controlador"},
    {"buttons seen by the game", "botões vistos pelo jogo", "botones detectados por el juego"},
    {"none", "nenhum", "ninguno"},
    {"frets", "trastes", "trastes"},
    {"green", "verde", "verde"},
    {"red", "vermelho", "rojo"},
    {"yellow", "amarelo", "amarillo"},
    {"blue", "azul", "azul"},
    {"orange", "laranja", "naranja"},
    {"strum", "palhetada", "rasgueo"},
    {"yes", "sim", "sí"},
    {"no", "não", "no"},
    {"Press the guitar's frets and strum. Nothing here means the guitar is not reaching the game.", "Aperte os trastes da guitarra e palhete. Se nada aparecer, a guitarra não está chegando ao jogo.", "Pulsa los trastes de la guitarra y rasguea. Si no aparece nada, la guitarra no está llegando al juego."},

    // Downloads
    {"charts from chorus encore", "charts do chorus encore", "charts de Chorus Encore"},
    {"Song, artist or charter", "Música, artista ou charter", "Canción, artista o charter"},
    {"newest charts - press Y to search", "charts mais recentes - aperte Y para buscar", "charts más recientes - pulsa Y para buscar"},
    {"charted by {}", "chart de {}", "chart de {}"},
    {"in library", "na biblioteca", "en la biblioteca"},
    {"No guitar charts matched.", "Nenhum chart de guitarra encontrado.", "No se encontraron charts de guitarra."},
    {"part", "parte", "parte"},
    {"intensity", "intensidade", "intensidad"},
    {"no part details", "sem detalhes das partes", "sin detalles de la parte"},
    {"peak {} notes/s", "pico de {} notas/s", "pico de {} notas/s"},
    {"solos", "solos", "solos"},
    {"open notes", "notas abertas", "notas abiertas"},
    {"tap notes", "notas tap", "notas tap"},
    {"also has {} (not playable)", "também tem {} (não jogável)", "también tiene {} (no jugable)"},
    {"Drums", "Bateria", "Batería"},
    {"Vocals", "Vocal", "Voz"},
    {"searching...", "buscando...", "buscando..."},
    {"added {}", "adicionada: {}", "añadida: {}"},
    {"{} guitar chart", "{} chart de guitarra", "{} chart de guitarra"},
    {"{} guitar charts", "{} charts de guitarra", "{} charts de guitarra"},
    {"searching", "buscando", "buscando"},
    {"connecting", "conectando", "conectando"},
    {"downloading", "baixando", "descargando"},
    {"unpacking", "descompactando", "descomprimiendo"},
    {"Download cancelled", "Download cancelado", "Descarga cancelada"},
    {"Could not start networking", "Não foi possível iniciar a rede", "No se pudo iniciar la red"},
    {"Cannot write to the songs folder", "Não foi possível gravar na pasta de músicas", "No se puede escribir en la carpeta de canciones"},

    // Calibration
    {"CALIBRATE", "CALIBRAR", "CALIBRAR"},
    {"visual offset", "atraso visual", "desfase visual"},
    {"audio offset", "atraso de áudio", "desfase de audio"},
    {"TAPS", "TOQUES", "TOQUES"},
    {"early", "cedo", "antes"},
    {"late", "tarde", "después"},
    {"median {} ms", "mediana {} ms", "mediana {} ms"},
    {"LISTEN", "OUÇA", "ESCUCHA"},
    {"TAP", "TOQUE", "TOCA"},
    {"Tap any fret or strum on every click.", "Toque qualquer traste ou palhete a cada clique.", "Pulsa cualquier traste o rasguea en cada clic."},
    {"Close your eyes if it helps.", "Feche os olhos se ajudar.", "Cierra los ojos si te ayuda."},
    {"Clicks are muted.", "Cliques sem som.", "Los clics están silenciados."},
    {"Tap as each note", "Toque quando cada nota", "Pulsa cuando cada nota"},
    {"crosses the line.", "cruzar a linha.", "cruce la línea."},
    {"VISUAL OFFSET", "ATRASO VISUAL", "DESFASE VISUAL"},
    {"AUDIO / INPUT OFFSET", "ATRASO DE ÁUDIO", "DESFASE DE AUDIO / ENTRADA"},
    {"was {} ms", "antes {} ms", "era {} ms"},
    {"taps were uneven - retry for a steadier read", "toques irregulares - repita para uma leitura melhor", "los toques fueron irregulares - inténtalo de nuevo para obtener una medición más estable"},

    // Playing
    {"GOOD", "BOM", "BIEN"},
    {"GREAT", "ÓTIMO", "GENIAL"},
    {"PERFECT", "PERFEITO", "PERFECTO"},
    {"EARLY", "CEDO", "PRONTO"},
    {"LATE", "TARDE", "TARDE"},
    {"SCORE", "PONTOS", "PUNTOS"},
    {"streak", "sequência", "racha"},
    {"{}% HIT", "{}% ACERTOS", "{}% ACIERTOS"},
    {"{} missed", "{} erros", "{} fallos"},
    {"BURNING", "PEGANDO FOGO", "ARDIENDO"},
    {"hit MINUS !", "aperte MENOS !", "pulsa MENOS !"},
    {"hit X !", "aperte X !", "pulsa X !"},
    {"no fail", "sem falha", "sin fallos"},
    {"danger!", "perigo!", "¡peligro!"},
    {"get ready", "prepare-se", "prepárate"},
    {"{} NOTE STREAK!", "SEQUÊNCIA DE {} NOTAS!", "¡RACHA DE {} NOTAS!"},
    {"Plus pause    strum with no frets for open notes    Minus star power", "Mais pausa    palhete sem trastes para notas abertas    Menos star power", "Más pausa    rasguea sin trastes para notas abiertas    Menos poder estrella"},
    {"Plus pause    any fret hits open notes    X star power", "Mais pausa    qualquer traste toca notas abertas    X star power", "Más pausa    cualquier traste toca notas abiertas    X poder estrella"},
    {"Plus pause    strum with no frets for open notes    X star power", "Mais pausa    palhete sem trastes para notas abertas    X star power", "Más pausa    rasguea sin trastes para notas abiertas    X poder estrella"},

    // Practice, solos and sections
    {"PRACTICE", "PRÁTICA", "PRÁCTICA"},
    {"loop a section until you nail it", "repita um trecho até acertar", "repite una sección hasta dominarla"},
    {"Pick where the loop starts.", "Escolha onde a repetição começa.", "Elige dónde empieza la repetición."},
    {"Pick where the loop ends.", "Escolha onde a repetição termina.", "Elige dónde termina la repetición."},
    {"start", "início", "inicio"},
    {"loop length {}", "duração da repetição {}", "duración de la repetición {}"},
    {"START HERE", "COMEÇAR AQUI", "EMPEZAR AQUÍ"},
    {"END HERE", "TERMINAR AQUI", "TERMINAR AQUÍ"},
    {"loop {}", "volta {}", "vuelta {}"},
    {"loop {}   last {}%", "volta {}   última {}%", "vuelta {}   último {}%"},
    {"Part {}", "Parte {}", "Parte {}"},
    {"RESTART LOOP", "REINICIAR VOLTA", "REINICIAR VUELTA"},
    {"CHANGE SECTIONS", "MUDAR SEÇÕES", "CAMBIAR SECCIONES"},
    {"PERFECT SOLO!", "SOLO PERFEITO!", "¡SOLO PERFECTO!"},
    {"solo bonus +{}", "bônus de solo +{}", "bonificación de solo +{}"},
    {"SECTIONS", "SEÇÕES", "SECCIONES"},
    {"SUMMARY", "RESUMO", "RESUMEN"},
    {"weakest: {}", "mais fraca: {}", "más débil: {}"},
    {"Rumble", "Vibração", "Vibración"},
    {"Hit ratings", "Avaliação dos acertos", "Evaluación de los aciertos"},
    {"PERFECT, GREAT or GOOD over the strike line after each hit, with an early/late marker.", "PERFEITO, ÓTIMO ou BOM sobre a linha a cada acerto, com um marcador de cedo/tarde.", "PERFECTO, GENIAL o BIEN sobre la línea después de cada acierto, con un indicador de antes/después."},
    {"A buzz when you miss, a pulse when star power kicks in, a double tap after a solo.", "Vibra ao errar, pulsa quando o star power começa e dá um toque depois de um solo.", "Vibra al fallar, pulsa cuando se activa el poder estrella y da un doble toque después de un solo."},

    // Download queue

    {"downloading {}%", "baixando {}%", "descargando {}%"},
    {"queued #{}", "na fila #{}", "en cola #{}"},
    {"{} more queued", "mais {} na fila", "{} más en cola"},
    {"QUEUE", "ADICIONAR", "COLA"},
    {"CANCEL ALL", "CANCELAR TUDO", "CANCELAR TODO"},

    // Multiplayer
    {"MULTIPLAYER", "MULTIJOGADOR", "MULTIJUGADOR"},
    {"two to four players, split screen", "de dois a quatro jogadores, tela dividida", "de dos a cuatro jugadores, pantalla dividida"},
    {"dock the console to play multiplayer", "encaixe o console na base para jogar em grupo", "coloca la consola en la base para jugar en grupo"},
    {"Multiplayer needs the console in its dock", "O multijogador precisa do console na base", "El multijugador necesita la consola en la base"},
    {"Multiplayer needs at least two players", "O multijogador precisa de pelo menos dois jogadores", "El multijugador necesita al menos dos jugadores"},
    {"Dock the console to carry on", "Encaixe o console na base para continuar", "Coloca la consola en la base para continuar"},
    {"Player {} has no chart for that difficulty", "O jogador {} não tem chart nessa dificuldade", "El jugador {} no tiene chart para esa dificultad"},
    {"PLAYER {}", "JOGADOR {}", "JUGADOR {}"},
    {"press A to join", "aperte A para entrar", "pulsa A para unirte"},
    {"up/down difficulty   left/right part", "cima/baixo dificuldade   esquerda/direita parte", "arriba/abajo dificultad   izquierda/derecha parte"},
    {"press PLUS to start", "aperte MAIS para começar", "pulsa MÁS para empezar"},
    {"at least two players needed", "são precisos pelo menos dois jogadores", "se necesitan al menos dos jugadores"},
    {"JOIN", "ENTRAR", "UNIRSE"},
    {"CHOOSE", "ESCOLHER", "ELEGIR"},
    {"difficulty", "dificuldade", "dificultad"},
    {"highway", "pista", "pista"},
    {"notes", "notas", "notas"},
    {"LEAVE", "SAIR", "SALIR"},
    {"{} streak", "sequência de {}", "racha de {}"},
    {"{}% hit", "{}% de acertos", "{}% de aciertos"},
    {"FINAL STANDINGS", "CLASSIFICAÇÃO FINAL", "CLASIFICACIÓN FINAL"},
    {"A plays again   B returns to the song list", "A joga de novo   B volta para a lista", "A juega de nuevo   B vuelve a la lista de canciones"},
    {"Film grain", "Granulado", "Grano de película"},
    {"The moving speckle over the picture. Turn it off first if frames drop.", "O granulado que se move sobre a imagem. Desligue primeiro se a imagem travar.", "El granulado que se mueve sobre la imagen. Desactívalo primero si bajan los fotogramas."},

    // Pause and results
    {"PAUSED", "PAUSADO", "EN PAUSA"},
    {"SONG FAILED", "VOCÊ FALHOU", "CANCIÓN FALLIDA"},
    {"SONG COMPLETE", "MÚSICA CONCLUÍDA", "CANCIÓN COMPLETADA"},
    {"NEW BEST!", "NOVO RECORDE!", "NUEVO RÉCORD!"},
    {"was {}", "antes {}", "era {}"},
    {"best {}", "recorde {}", "récord {}"},
    {"FINAL SCORE", "PONTUAÇÃO FINAL", "PUNTUACIÓN FINAL"},
    {"the crowd is booing", "a plateia está vaiando", "el público está abucheando"},
    {"flawless!", "impecável!", "¡impecable!"},
    {"shredded it", "detonou", "brutal"},
    {"solid set", "show sólido", "buena actuación"},
    {"not bad", "nada mal", "no está mal"},
    {"keep practicing", "continue treinando", "sigue practicando"},
    {"notes hit", "notas acertadas", "notas acertadas"},
    {"best streak", "maior sequência", "mejor racha"},
    {"perfect / great / good", "perfeito / ótimo / bom", "perfecto / genial / bien"},
    {"accuracy", "precisão", "precisión"},
    {"the amp is still humming", "o amplificador continua ligado", "el amplificador sigue zumbando"},
    {"RESTART", "RECOMEÇAR", "REINICIAR"},
    {"CHANGE DIFFICULTY", "MUDAR DIFICULDADE", "CAMBIAR DIFICULTAD"},
    {"QUIT TO SONG LIST", "SAIR PARA A LISTA", "SALIR A LA LISTA DE CANCIONES"},
    {"TIMING FIXED", "TEMPO AJUSTADO", "TIEMPO AJUSTADO"},
    {"FIX TIMING ({} MS)", "AJUSTAR ({} MS)", "AJUSTAR ({} MS)"},
    {"audio offset now {} ms", "atraso de áudio agora em {} ms", "desfase de audio ahora en {} ms"},
    {"you hit {} ms late on average", "você tocou {} ms atrasado, em média", "has tocado {} ms tarde de media"},
    {"you hit {} ms early on average", "você tocou {} ms adiantado, em média", "has tocado {} ms antes de media"},
};

const std::unordered_map<std::string_view, const char *> &portugueseMap() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const char *> m;
        for (const auto &[en, pt, es] : translationTable)
            m.emplace(en, pt);
        return m;
    }();
    return map;
}

const std::unordered_map<std::string_view, const char *> &spanishMap() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const char *> m;
        for (const auto &[en, pt, es] : translationTable)
            m.emplace(en, es);
        return m;
    }();
    return map;
}

} // namespace

const char *nativeName(Language l) {
    switch (l) {
        case Language::Portuguese: return "Português";
        case Language::Spanish:    return "Español";
        default:                   return "English";
    }
}
void set(Language l) { language = l; }
Language current() { return language; }

const char *tr(const char *english) {
    if (current() == Language::English)
        return english;

    if (current() == Language::Portuguese) {
        const auto &map = portugueseMap();
        const auto it = map.find(english);
        return it == map.end() ? english : it->second;
    }

    if (current() == Language::Spanish) {
        const auto &map = spanishMap();
        const auto it = map.find(english);
        return it == map.end() ? english : it->second;
    }

    return english;
}

std::string tr(const std::string &english) {
    if (current() == Language::English)
        return english;

    if (current() == Language::Portuguese) {
        const auto &map = portugueseMap();
        const auto it = map.find(english);
        return it == map.end() ? english : it->second;
    }

    if (current() == Language::Spanish) {
        const auto &map = spanishMap();
        const auto it = map.find(english);
        return it == map.end() ? english : it->second;
    }

    return english;
}

std::string fill(const std::string &pattern, std::initializer_list<std::string> args) {
    std::string out;
    auto next = args.begin();
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}' && next != args.end()) {
            out += *next++;
            ++i;
        } else
            out += pattern[i];
    }
    return out;
}
} // namespace fret::lang
