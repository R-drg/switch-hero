# Motions da 0.3

Vídeos curtos em 16:9 (1920x1080, 30 fps) com as novidades da 0.3, no visual
do jogo: parede grunge com luz de CRT, grão de filme, fita adesiva, painéis de
metal escovado, as fontes de `assets/fonts` e as pistas e paletas de notas de
`src/main.cpp` e `src/look.cpp`.

| Cena | Duração | O que mostra |
|---|---|---|
| `00-abertura` | 5,5 s | Logo, disco e o selo "versão 0.3" |
| `01-multijogador` | 9 s | Uma pista vira duas e depois quatro, cada uma com seu tema |
| `02-pratica` | 8 s | Escolha de início e fim da repetição, contagem e voltas |
| `03-solos` | 8 s | Precisão por seção, trilhos azuis no solo, "SOLO PERFEITO!" |
| `04-personalizar` | 9 s | As 13 pistas e as 24 paletas passando na prévia |
| `05-idioma` | 6,5 s | Seletor de idioma e os textos virando português |
| `06-desempenho` | 8 s | Draw calls e chamadas ao sistema de arquivos, antes e depois |
| `07-e-mais` | 9 s | Whammy, vibração e fila de downloads |
| `08-encerramento` | 6 s | Logo, "0.3 já disponível" e onde baixar |

`switch-hero-0.3.mp4` junta todas em sequência (cerca de 69 s).

## Gerar os vídeos

As cenas são desenhadas num canvas em `motion.html`. `render.mjs` abre a página
no Chromium headless, desenha quadro a quadro e manda os quadros para o
ffmpeg. Precisa de Node, Playwright e um ffmpeg com libx264 (defina `FFMPEG`
se ele não estiver no `PATH`). Os vídeos saem em `promo/0.3/out/`.

```sh
node promo/0.3/render.mjs                    # todas as cenas e o vídeo completo
node promo/0.3/render.mjs 04-personalizar    # uma cena
node promo/0.3/render.mjs --stills /tmp/x    # três quadros de cada cena
```

Para ver no navegador, sirva a raiz do repositório (as fontes vêm de
`assets/fonts`) e abra `promo/0.3/motion.html?play`, ou
`motion.html?scene=01-multijogador&t=4.5` para um quadro só.
