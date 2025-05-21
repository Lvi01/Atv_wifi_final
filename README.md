# 🌱 Projeto GreenLife Integrado - IoT para Monitoramento de Plantas

## ✅ Objetivo Geral

O objetivo do projeto **GreenLife** é desenvolver uma aplicação embarcada capaz de monitorar em tempo real os níveis de umidade e temperatura simulados para uma planta, utilizando uma interface web acessível via Wi-Fi, além de fornecer feedback físico direto através de **LEDs**, **display OLED**, **buzzer** e **matriz de LEDs RGB**.

O sistema busca unir **monitoramento remoto e local**, oferecendo uma experiência completa de interação com o ambiente simulado da planta. Além disso, permite a **alternância entre modos de leitura automática (sensor interno)** e **manual (joystick analógico)**, aplicando conceitos de **Internet das Coisas (IoT)**, sensoriamento, interfaces embarcadas e conectividade sem fio.

---

## ⚙️ Descrição Funcional

O sistema opera como um **servidor HTTP embarcado**, construído em C com o SDK oficial da Raspberry Pi Pico W e a pilha de rede **lwIP**.

### Fluxo Geral:

1. **Inicialização de periféricos**: ADC, PWM, I²C, botões, PIO, Wi-Fi.
2. **Conexão Wi-Fi**: usando o módulo CYW43 em modo STA.
3. **Servidor TCP**: escuta a porta 80 e responde requisições HTTP com os dados da planta em tempo real.
4. **Leitura dos sensores**:
   - Umidade: sempre via joystick (ADC0).
   - Temperatura:
     - **Modo automático**: sensor interno da Pico (ADC4).
     - **Modo manual**: joystick (ADC1).
5. **Alternância de modo**: feita por botão físico (GPIO5) ou botão na interface web (`/toggle_modo`).
6. **Feedback ao usuário**:
   - Interface web com atualização automática.
   - Display OLED com status e dados.
   - LEDs RGB indicando o estado.
   - Matriz de LEDs com animação da planta.
   - Alarme sonoro em caso crítico.

### Interface Web:

- Desenvolvida em HTML dinâmico com autoatualização (`<meta refresh>`).
- Mostra temperatura, umidade, estado da planta e modo ativo.
- Permite alternar modo entre Manual e Automático via botão.

### Lógica de Estado da Planta:

A função `interpretar_estado_planta()` avalia os dados e define mensagens como:

- "Sua planta está feliz!"
- "Sua planta está em perigo!"
- "Com calor", "Com frio", "Com sede", "Excesso de água"

Essas mensagens são exibidas no navegador e no terminal serial.

---

## 🔌 Uso dos Periféricos da BitDogLab

### 🎮 Joystick Analógico

- **Eixo X (ADC1 / GPIO27)** → Simula **temperatura** (0–60 °C)
- **Eixo Y (ADC0 / GPIO26)** → Simula **umidade** (0–100%)
- Usado no modo **manual** para simular condições ambientais.

### 🔘 Botão Físico (GPIO5)

- Permite alternar entre **modo automático e manual**.
- Configurado com interrupção e debounce via timestamp.
- Reflete mudanças tanto no sistema local quanto na interface web.

### 📶 Wi-Fi CYW43

- Conecta à rede Wi-Fi como estação (STA).
- Permite que navegadores acessem o sistema via IP local.
- Exibe informações atualizadas em tempo real e aceita comandos GET.

### 💡 LEDs RGB

- **Verde**: Planta em condição ideal.
- **Laranja**: Temperatura ou umidade fora do ideal.
- **Vermelho**: Estado crítico, dispara alarme.

### 🔊 Buzzer (GPIO21)

- Ativado em estado crítico após 5s.
- Emite som alternando entre duas frequências (PWM).
- Desativado ao pressionar o botão físico.

### 🟩 Matriz de LEDs via PIO (GPIO7)

- Mostra a **planta estilizada** com cores variáveis:
  - **Verde**: saudável
  - **Vermelho**: quente
  - **Azul**: frio
- Controlada com código customizado `.pio`.

### 📺 Display OLED (I²C)

- Mostra dados numéricos da temperatura e umidade.
- Exibe o modo atual (Manual ou Auto).
- Alerta visual quando o alarme está ativado.

---

## 🧠 Funcionalidades Inteligentes

- Alarme automático com temporização crítica.
- Alternância de modos remota e física.
- Lógica de avaliação contextual (temp/umidade).
- Terminal serial com relatórios periódicos.
- Página web atualizada automaticamente.

---

### 📡 Tecnologias Aplicadas

- C SDK (Pico)
- lwIP (TCP/IP Stack)
- PWM, ADC, I²C, PIO
- Wi-Fi integrado CYW43
- HTML embarcado (servidor web nativo)

---

## 👨‍💻 Autor

Desenvolvido por Levi Silva Freitas  
CEPEDI - Embarcatech TIC37  

