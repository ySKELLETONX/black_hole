# **black**_**hole** — fork com renderizador aprimorado

🇺🇸 [Read in English](README.md)

![Buraco negro renderizado pelo BlackHole3D_GPU em 2560x1440](docs/screenshot_2k.png)
<sub>BlackHole3D_GPU, render em 2560x1440.</sub>

Fork de [kavan010/black_hole](https://github.com/kavan010/black_hole) por **SKELLETONX**, com o renderizador de GPU da simulação 3D (`black_hole.cpp` + `geodesic.comp`) reescrito.

### Novidades
- **Ray tracing em tempo real na resolução total da janela**: as geodésicas dos fótons no espaço-tempo de Schwarzschild são integradas com RK4 de passo adaptativo (antes era 200x150).
- **Disco de acreção fisicamente baseado**: cor de corpo negro a partir do perfil de temperatura de Novikov–Thorne, feixe relativístico por efeito Doppler, redshift gravitacional e gás turbulento animado com rotação diferencial kepleriana.
- **Céu com lente gravitacional**: campo de estrelas procedural, faixa da Via Láctea e nebulosas.
- **Pipeline HDR**: bloom a partir da cadeia de mipmaps, tone mapping fílmico ACES e vinheta.
- **Painel de controle ao vivo** ([Dear ImGui](https://github.com/ocornut/imgui)) com ajustes do disco, do céu, da câmera, do pós-processamento e da qualidade.
- Câmera orbital suave, órbita automática e screenshots em PNG.

### Build rápido (Windows, sem vcpkg)
Requer o Visual Studio 2022+ ou o Build Tools com a carga de trabalho de C++. Execute:

```
build_gpu.bat
```

Na primeira execução, o script baixa GLFW, GLEW, GLM, Dear ImGui e stb para `deps/` e gera `build_gpu\BlackHole3D_GPU.exe`. Execute o programa de dentro da pasta `build_gpu`, junto dos arquivos de shader.

### Controles
| Entrada | Ação |
|---|---|
| Arrastar com o botão esquerdo | Girar a câmera |
| Roda do mouse | Zoom |
| H | Mostrar / ocultar o painel |
| Espaço | Ligar / desligar órbita automática |
| P | Salvar screenshot (PNG) |
| M | Silenciar / reativar a música |
| G / botão direito | Ligar / segurar a gravidade entre os corpos |
| Esc | Sair |

### Música de fundo
Coloque um arquivo chamado `music.mp3` (ou `music.wav` / `music.flac`) em `docs/` (ou ao lado do `BlackHole3D_GPU.exe`). Ele toca em loop, com fade-in, quando o programa abre, e o painel tem controle de volume e botão de mudo. O repositório não inclui nenhuma música e os arquivos de música são ignorados pelo Git, então use uma faixa que você tenha direito de usar.

Linha de comando (renderiza um único frame fora da tela e fecha):

```
BlackHole3D_GPU.exe --screenshot saida.png --width 2560 --height 1440 --az 30 --elev 10 --dist 20
```

| Opção | Significado |
|---|---|
| `--screenshot arquivo.png` | Salva o frame em PNG e fecha |
| `--width` / `--height` | Resolução da imagem (não é limitada pelo monitor) |
| `--az` | Ângulo da câmera ao redor do buraco negro (graus) |
| `--elev` | Altura da câmera acima do disco (graus) |
| `--dist` | Distância da câmera em raios de Schwarzschild |
| `--time` | Instante da animação do disco (segundos) |

---

## README original (traduzido)

Projeto de simulação de buraco negro.

Aqui está o código bruto do buraco negro; tudo vai ficar dentro de uma pasta src/bin caso você queira copiar os arquivos.

Estou escrevendo isto no começo do projeto (espero terminá-lo ;D). Este é o plano:

1. Ray tracing: adicionar ray tracing à simulação de gravidade para simular a lente gravitacional.

2. Disco de acreção: simular o disco de acreção usando o ray tracing + os halos.

3. Curvatura do espaço-tempo: mostrar visualmente o "alçapão no espaço-tempo" que é um buraco negro, usando uma grade do espaço-tempo.

4. [opcional] tentar fazer rodar em tempo real ;D

Espero que funcione :/

Edição: depois de concluir o projeto:

## **Requisitos para compilar:**

1. Compilador C++ com suporte a C++ 17 ou mais recente

2. [CMake](https://cmake.org/)

3. [Vcpkg](https://vcpkg.io/en/)

4. [Git](https://git-scm.com/)

## **Instruções de compilação:**

1. Clone o repositório:
	- `git clone https://github.com/ySKELLETONX/black_hole.git`
2. Entre na pasta clonada:
	- `cd ./black_hole`
3. Instale as dependências com o vcpkg:
	- `vcpkg install`
4. Descubra o caminho do toolchain do vcpkg para o CMake:
	- `vcpkg integrate install`
	- A saída será algo como: `CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"`
5. Crie uma pasta de build:
	- `mkdir build`
6. Configure o projeto com o CMake:
	- `cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake`
	- Use o caminho do toolchain do vcpkg obtido acima
7. Compile o projeto:
	- `cmake --build build`
8. Execute o programa:
	- Os executáveis ficam na pasta build

### Alternativa: pacotes apt no Debian/Ubuntu

Se você não quiser usar o vcpkg, ou só precisar de um jeito rápido de instalar os pacotes de desenvolvimento no Debian/Ubuntu, instale estes pacotes e depois siga os passos normais do CMake acima:

```bash
sudo apt update
sudo apt install build-essential cmake \
	libglew-dev libglfw3-dev libglm-dev libgl1-mesa-dev
```

Eles fornecem os arquivos de desenvolvimento de GLEW, GLFW, GLM e OpenGL, para que as chamadas `find_package(...)` do `CMakeLists.txt` encontrem as bibliotecas. Depois de instalar, rode `cmake -B build -S .` e `cmake --build build`, como nas instruções de compilação.

## **Como o código funciona:**
2D: simples, basta executar o `2D_lensing.cpp` com as dependências necessárias instaladas.

3D: `black_hole.cpp` e `geodesic.comp` trabalham juntos para rodar a simulação mais rápido na GPU. O `black_hole.cpp` envia os dados da cena (câmera, disco e objetos) e o compute shader `geodesic.comp` faz os cálculos pesados com esses dados.

Deve funcionar com as dependências instaladas, mas o autor original só testou no Windows com a própria GPU.
