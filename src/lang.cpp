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
const std::pair<const char *, const char *> portugueseTable[] = {
    // Start-up and title
    {"looking for songs", "procurando músicas"},
    {"reading your sd card", "lendo seu cartão sd"},
    {"found {} song", "{} música encontrada"},
    {"found {} songs", "{} músicas encontradas"},
    {"burned mixtape vol. 1", "mixtape gravada vol. 1"},
    {"{} SONG", "{} MÚSICA"},
    {"{} SONGS", "{} MÚSICAS"},
    {"QUICKPLAY", "JOGO RÁPIDO"},
    {"DOWNLOAD SONGS", "BAIXAR MÚSICAS"},
    {"OPTIONS", "OPÇÕES"},
    {"QUIT", "SAIR"},
    {"pick a song from your library", "escolha uma música da sua biblioteca"},
    {"grab charts from chorus encore", "baixe charts do chorus encore"},
    {"controls, calibration and gameplay", "controles, calibração e jogabilidade"},
    {"back to the homebrew menu", "voltar ao menu homebrew"},
    {"CHOOSE YOUR LANGUAGE", "ESCOLHA O IDIOMA"},
    {"you can change it later in options", "dá para mudar depois em opções"},

    // Hints
    {"SELECT", "ESCOLHER"},
    {"BACK", "VOLTAR"},
    {"PLAY", "TOCAR"},
    {"CONFIRM", "CONFIRMAR"},
    {"CANCEL", "CANCELAR"},
    {"SELECT SONG", "ESCOLHER"},
    {"DOWNLOAD", "BAIXAR"},
    {"DELETE", "APAGAR"},
    {"SORT", "ORDENAR"},
    {"OPEN", "ABRIR"},
    {"SAVE & BACK", "SALVAR E VOLTAR"},
    {"CHANGE", "MUDAR"},
    {"CLOSE", "FECHAR"},
    {"SEARCH", "BUSCAR"},
    {"KEEP", "MANTER"},
    {"RETRY", "REPETIR"},
    {"CONTINUE", "CONTINUAR"},
    {"RESUME", "RETOMAR"},

    // Parts and difficulties
    {"Guitar", "Guitarra"},
    {"Bass", "Baixo"},
    {"Rhythm", "Base"},
    {"Co-op", "Cooperativo"},
    {"Keys", "Teclado"},
    {"Easy", "Fácil"},
    {"Medium", "Médio"},
    {"Hard", "Difícil"},

    // Song setup
    {"SELECT INSTRUMENT", "ESCOLHA O INSTRUMENTO"},
    {"SELECT DIFFICULTY", "ESCOLHA A DIFICULDADE"},
    {"{} notes", "{} notas"},
    {"not charted", "sem chart"},
    {"NO FAIL", "SEM FALHA"},
    {"NO FAIL: ON", "SEM FALHA: SIM"},
    {"NO FAIL: OFF", "SEM FALHA: NÃO"},
    {"wii guitar mode", "modo guitarra de wii"},
    {"press-to-hit mode", "modo toque direto"},
    {"strum mode", "modo palhetada"},
    {"BEST {}", "RECORDE {}"},

    // Song list
    {"pick a track off the mixtape", "escolha uma faixa da mixtape"},
    {"by {}", "por {}"},
    {"TITLE", "TÍTULO"},
    {"ARTIST", "ARTISTA"},
    {"STARS", "ESTRELAS"},
    {"NEW", "NOVAS"},
    {"NO SONGS FOUND", "NENHUMA MÚSICA"},
    {"Copy extracted Clone Hero song folders into", "Copie as pastas de músicas do Clone Hero, extraídas, para"},
    {"Each folder needs notes.chart or notes.mid plus audio", "Cada pasta precisa de notes.chart ou notes.mid e o áudio"},
    {"or press + to download charts", "ou aperte + para baixar charts"},
    {"UNSUPPORTED SONG", "MÚSICA NÃO SUPORTADA"},
    {"reading chart", "lendo chart"},
    {"+ {} more", "+ {} outros"},
    {"{} STEM", "{} FAIXA"},
    {"{} STEMS", "{} FAIXAS"},
    {"chart warnings", "avisos no chart"},
    {"not played on {} {}", "ainda não jogada: {} {}"},
    {"DELETE SONG?", "APAGAR MÚSICA?"},
    {"Removes this folder and everything in it from the SD card.",
     "Remove esta pasta e tudo o que há nela do cartão SD."},
    {"This can't be undone.", "Isso não pode ser desfeito."},
    {"KEEP IT", "MANTER"},
    {"Deleted {}", "Apagada: {}"},

    // Options
    {"tune your rig", "ajuste seu equipamento"},
    {"GAMEPLAY", "JOGABILIDADE"},
    {"controller mode, no fail, note speed, lefty", "modo de controle, sem falha, velocidade, canhoto"},
    {"AUDIO", "ÁUDIO"},
    {"music and sound effect volume", "volume da música e dos efeitos"},
    {"AUDIO / VIDEO SYNC", "SINCRONIA ÁUDIO / VÍDEO"},
    {"calibrate if notes feel early or late", "calibre se as notas parecerem fora do tempo"},
    {"CONTROLS", "CONTROLES"},
    {"fret buttons and controller test", "botões dos trastes e teste do controle"},
    {"LANGUAGE", "IDIOMA"},
    {"English or Portuguese", "inglês ou português"},
    {"Options save when you leave this screen.", "As opções são salvas quando você sai desta tela."},
    {"Save and go back.", "Salvar e voltar."},
    {"ON", "LIGADO"},
    {"OFF", "DESLIGADO"},
    {"TO START", "PARA COMEÇAR"},
    {"TO OPEN", "PARA ABRIR"},
    {"AGAIN TO CONFIRM", "DE NOVO PARA CONFIRMAR"},
    {"PRESS A BUTTON...", "APERTE UM BOTÃO..."},
    {"Controller mode", "Modo de controle"},
    {"WII GUITAR", "GUITARRA DE WII"},
    {"PRESS-TO-HIT", "TOQUE DIRETO"},
    {"STRUM", "PALHETADA"},
    {"Wii guitar over Bluetooth (needs the MissionControl patch). Fixed frets, strum bar strums.",
     "Guitarra de Wii por Bluetooth (requer o patch do MissionControl). Trastes fixos, palheta na barra."},
    {"Press each fret as its note arrives. Best on Joy-Cons and the Pro Controller.",
     "Aperte cada traste quando a nota chegar. Melhor com Joy-Cons e o Pro Controller."},
    {"Hold the frets and strum with the D-pad. Hammer-ons and taps need no strum.",
     "Segure os trastes e palhete com o direcional. Hammer-ons e taps não precisam de palhetada."},
    {"No fail", "Sem falha"},
    {"The rock meter can never fail you. Good for learning a song.",
     "O medidor de rock nunca te derruba. Bom para aprender uma música."},
    {"Miss too much and the crowd boos you off stage.", "Erre demais e a plateia te vaia para fora do palco."},
    {"Hit window", "Janela de acerto"},
    {"STRICT", "RIGOROSA"},
    {"NORMAL", "NORMAL"},
    {"LENIENT", "TOLERANTE"},
    {"How far off a note can be and still count: +-{} ms on Expert, +-{} ms on Easy.",
     "Quanto uma nota pode desviar e ainda contar: +-{} ms no Expert, +-{} ms no Fácil."},
    {"Note speed", "Velocidade das notas"},
    {"Faster notes spread out a busy chart. Each note is on screen for {} ms.",
     "Notas mais rápidas espalham um chart cheio. Cada nota fica {} ms na tela."},
    {"Lefty flip", "Modo canhoto"},
    {"Mirrors the highway so green is on the right, for left-handed players.",
     "Espelha a pista para o verde ficar à direita, para quem é canhoto."},
    {"Music volume", "Volume da música"},
    {"The song and its previews on the song list.", "A música e as prévias na lista de músicas."},
    {"Sound effects volume", "Volume dos efeitos"},
    {"Menu sounds, the count-in, misses and star power.", "Sons do menu, contagem, erros e star power."},
    {"Calibrate audio", "Calibrar áudio"},
    {"Do this first. Tap along to a click track by ear to measure your audio delay.",
     "Faça isto primeiro. Toque junto com os cliques, de ouvido, para medir o atraso do áudio."},
    {"Audio / input offset", "Atraso de áudio / entrada"},
    {"Fine-tune by hand in 5 ms steps. Positive judges notes later.",
     "Ajuste fino em passos de 5 ms. Positivo avalia as notas mais tarde."},
    {"Calibrate video", "Calibrar vídeo"},
    {"Tap as silent notes cross the line to line the highway up with the music.",
     "Toque quando as notas cruzarem a linha, sem som, para alinhar a pista com a música."},
    {"Visual offset", "Atraso visual"},
    {"Fine-tune by hand in 5 ms steps. Positive draws notes later.",
     "Ajuste fino em passos de 5 ms. Positivo desenha as notas mais tarde."},
    {"Timing overlay", "Medidor de desempenho"},
    {"Shows frame times and audio clock drift in the corner. For checking smoothness.",
     "Mostra o tempo dos quadros e o desvio do áudio no canto. Para checar a fluidez."},
    {"Green fret", "Traste verde"},
    {"Red fret", "Traste vermelho"},
    {"Yellow fret", "Traste amarelo"},
    {"Blue fret", "Traste azul"},
    {"Orange fret", "Traste laranja"},
    {"Fixed in Wii guitar mode.", "Fixo no modo guitarra de Wii."},
    {"Press A, then the button or trigger to use.", "Aperte A e depois o botão ou gatilho que quer usar."},
    {"Controller test", "Teste do controle"},
    {"Shows every button the game sees. Use it when a fret or guitar is not responding.",
     "Mostra cada botão que o jogo recebe. Use quando um traste ou a guitarra não responder."},
    {"Reset all options", "Restaurar opções"},
    {"Puts every option, offset and button back to its default.",
     "Volta todas as opções, atrasos e botões para o padrão."},
    {"Language", "Idioma"},
    {"Menus and messages. Song titles stay as they are.", "Menus e mensagens. Os nomes das músicas não mudam."},
    {"Press the button or trigger for this fret. Minus cancels.",
     "Aperte o botão ou gatilho para este traste. Menos cancela."},
    {"Already assigned to another fret", "Já está em outro traste"},
    {"Options reset to defaults", "Opções restauradas para o padrão"},
    {"Controller mode reset to press-to-hit", "Modo de controle voltou para toque direto"},
    {"Controller disconnected", "Controle desconectado"},

    // Controller test
    {"CONTROLLER TEST", "TESTE DO CONTROLE"},
    {"normal controller", "controle normal"},
    {"player one (guitar)", "jogador um (guitarra)"},
    {"guitar mode", "modo guitarra"},
    {"connected", "conectado"},
    {"not connected", "desconectado"},
    {"on", "ligado"},
    {"off", "desligado"},
    {"styles (normal/player one)", "estilos (normal/jogador um)"},
    {"raw buttons", "botões brutos"},
    {"controller", "controle"},
    {"no controller", "nenhum controle"},
    {"buttons seen by the game", "botões vistos pelo jogo"},
    {"none", "nenhum"},
    {"frets", "trastes"},
    {"green", "verde"},
    {"red", "vermelho"},
    {"yellow", "amarelo"},
    {"blue", "azul"},
    {"orange", "laranja"},
    {"strum", "palhetada"},
    {"yes", "sim"},
    {"no", "não"},
    {"Press the guitar's frets and strum. Nothing here means the guitar is not reaching the game.",
     "Aperte os trastes da guitarra e palhete. Se nada aparecer, a guitarra não está chegando ao jogo."},

    // Downloads
    {"charts from chorus encore", "charts do chorus encore"},
    {"Song, artist or charter", "Música, artista ou charter"},
    {"newest charts - press Y to search", "charts mais recentes - aperte Y para buscar"},
    {"charted by {}", "chart de {}"},
    {"in library", "na biblioteca"},
    {"No guitar charts matched.", "Nenhum chart de guitarra encontrado."},
    {"part", "parte"},
    {"intensity", "intensidade"},
    {"no part details", "sem detalhes das partes"},
    {"peak {} notes/s", "pico de {} notas/s"},
    {"solos", "solos"},
    {"open notes", "notas abertas"},
    {"tap notes", "notas tap"},
    {"also has {} (not playable)", "também tem {} (não jogável)"},
    {"Drums", "Bateria"},
    {"Vocals", "Vocal"},
    {"searching...", "buscando..."},
    {"added {}", "adicionada: {}"},
    {"{} guitar charts", "{} charts de guitarra"},
    {"searching", "buscando"},
    {"connecting", "conectando"},
    {"downloading", "baixando"},
    {"unpacking", "descompactando"},
    {"Download cancelled", "Download cancelado"},
    {"Could not start networking", "Não foi possível iniciar a rede"},
    {"Cannot write to the songs folder", "Não foi possível gravar na pasta de músicas"},

    // Calibration
    {"CALIBRATE", "CALIBRAR"},
    {"visual offset", "atraso visual"},
    {"audio offset", "atraso de áudio"},
    {"TAPS", "TOQUES"},
    {"early", "cedo"},
    {"late", "tarde"},
    {"median {} ms", "mediana {} ms"},
    {"LISTEN", "OUÇA"},
    {"TAP", "TOQUE"},
    {"Tap any fret or strum on every click.", "Toque qualquer traste ou palhete a cada clique."},
    {"Close your eyes if it helps.", "Feche os olhos se ajudar."},
    {"Clicks are muted.", "Cliques sem som."},
    {"Tap as each note", "Toque quando cada nota"},
    {"crosses the line.", "cruzar a linha."},
    {"VISUAL OFFSET", "ATRASO VISUAL"},
    {"AUDIO / INPUT OFFSET", "ATRASO DE ÁUDIO"},
    {"was {} ms", "antes {} ms"},
    {"taps were uneven - retry for a steadier read", "toques irregulares - repita para uma leitura melhor"},

    // Playing
    {"GOOD", "BOM"},
    {"GREAT", "ÓTIMO"},
    {"PERFECT", "PERFEITO"},
    {"EARLY", "CEDO"},
    {"LATE", "TARDE"},
    {"SCORE", "PONTOS"},
    {"streak", "sequência"},
    {"{}% HIT", "{}% ACERTOS"},
    {"{} missed", "{} erros"},
    {"BURNING", "PEGANDO FOGO"},
    {"hit MINUS !", "aperte MENOS !"},
    {"hit X !", "aperte X !"},
    {"no fail", "sem falha"},
    {"danger!", "perigo!"},
    {"get ready", "prepare-se"},
    {"{} NOTE STREAK!", "SEQUÊNCIA DE {} NOTAS!"},
    {"Plus pause    strum with no frets for open notes    Minus star power",
     "Mais pausa    palhete sem trastes para notas abertas    Menos star power"},
    {"Plus pause    any fret hits open notes    X star power",
     "Mais pausa    qualquer traste toca notas abertas    X star power"},
    {"Plus pause    strum with no frets for open notes    X star power",
     "Mais pausa    palhete sem trastes para notas abertas    X star power"},

    // Pause and results
    {"PAUSED", "PAUSADO"},
    {"SONG FAILED", "VOCÊ FALHOU"},
    {"SONG COMPLETE", "MÚSICA CONCLUÍDA"},
    {"NEW BEST!", "NOVO RECORDE!"},
    {"was {}", "antes {}"},
    {"best {}", "recorde {}"},
    {"FINAL SCORE", "PONTUAÇÃO FINAL"},
    {"the crowd is booing", "a plateia está vaiando"},
    {"flawless!", "impecável!"},
    {"shredded it", "detonou"},
    {"solid set", "show sólido"},
    {"not bad", "nada mal"},
    {"keep practicing", "continue treinando"},
    {"notes hit", "notas acertadas"},
    {"best streak", "maior sequência"},
    {"perfect / great / good", "perfeito / ótimo / bom"},
    {"accuracy", "precisão"},
    {"the amp is still humming", "o amplificador continua ligado"},
    {"RESTART", "RECOMEÇAR"},
    {"CHANGE DIFFICULTY", "MUDAR DIFICULDADE"},
    {"QUIT TO SONG LIST", "SAIR PARA A LISTA"},
    {"TIMING FIXED", "TEMPO AJUSTADO"},
    {"FIX TIMING ({} MS)", "AJUSTAR ({} MS)"},
    {"audio offset now {} ms", "atraso de áudio agora em {} ms"},
    {"you hit {} ms late on average", "você tocou {} ms atrasado, em média"},
    {"you hit {} ms early on average", "você tocou {} ms adiantado, em média"},
};

const std::unordered_map<std::string_view, const char *> &portugueseMap() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const char *> m;
        for (const auto &[en, pt] : portugueseTable)
            m.emplace(en, pt);
        return m;
    }();
    return map;
}
} // namespace

const char *nativeName(Language l) { return l == Language::Portuguese ? "Português" : "English"; }
void set(Language l) { language = l; }
Language current() { return language; }

const char *tr(const char *english) {
    if (current() == Language::English)
        return english;
    const auto &map = portugueseMap();
    const auto it = map.find(english);
    return it == map.end() ? english : it->second;
}
std::string tr(const std::string &english) {
    if (current() == Language::English)
        return english;
    const auto &map = portugueseMap();
    const auto it = map.find(english);
    return it == map.end() ? english : it->second;
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
