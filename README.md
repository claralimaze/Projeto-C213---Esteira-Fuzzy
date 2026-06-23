# Esteira Transportadora Inteligente com Controle Fuzzy (C213 - INATEL)

Autores - Clara de Lima Azevedo, Guilherme Felipe Ribeiro, Tiago Gregorio.

Controle de **velocidade** de uma esteira transportadora industrial usando **lógica fuzzy**
embarcada em **ESP32**, com comunicação **MQTT** e **dashboard Node-RED** em tempo real.

---

## 1. Visão geral

| Item | Descrição |
|------|-----------|
| Planta | Esteira transportadora (modelo dinâmico de 1a ordem) |
| Variável controlada | Velocidade do motor (RPM) |
| Variável manipulada | PWM do motor (0-100%) |
| Perturbação | Carga sobre a esteira (potenciômetro + peso das caixas) |
| Controlador | Fuzzy Mamdani: 2 entradas (erro, dErro), 25 regras, centroide |
| Atuador | Servo (visual no Wokwi) / Motor CC + driver (hardware real) |
| Sensores | Potenciômetro (carga), HC-SR04 (contagem/dimensão de caixas) |
| Sinalização | LCD I2C, LEDs (verde/amarelo/vermelho), buzzer (sirene) |
| Comunicação | MQTT sobre TLS (HiveMQ Cloud) |
| Dashboard | Node-RED: medidores, gráfico, set points, pertinência e regras |

## 2. Estrutura do repositório

```
esteira_fuzzy.ino / sketch.ino   Firmware da ESP32 (comentado)
diagram.json                     Circuito para simulação no Wokwi
libraries.txt                    Bibliotecas necessárias
dashboard_nodered.json           Fluxo do dashboard (importar no Node-RED)
secrets_example.h                Modelo de credenciais (NÃO versionar a real)
.gitignore                       Ignora secrets.h
figs/                            Pertinências, superfície de controle, respostas
docs/                            Relatório, guias e documento de código comentado
```

## 3. Como rodar (resumo)

1. **Wokwi:** crie um projeto ESP32, cole `sketch.ino` e `diagram.json`, adicione as
   bibliotecas de `libraries.txt`, preencha a senha MQTT e dê play.
2. **Node-RED:** `npm install -g node-red`, rode `node-red`, instale o
   `node-red-dashboard`, importe `dashboard_nodered.json`, preencha as credenciais do
   broker (aba Segurança) e faça Deploy.
3. **Painel:** abra `http://127.0.0.1:1880/ui`.

Passo a passo detalhado em `docs/Guia_Teste.pdf`.

## 4. Mapa de pinos

| Componente | Pino ESP32 |
|------------|-----------|
| Potenciômetro (carga) | GPIO 34 |
| Servo / motor | GPIO 18 |
| LED verde / amarelo / vermelho | GPIO 25 / 26 / 27 |
| Display I2C (SDA/SCL) | GPIO 21 / 22 |
| Buzzer | GPIO 32 |
| HC-SR04 (TRIG / ECHO) | GPIO 5 / 23 |
| Botão de emergência | GPIO 33 |

## 5. Segurança das credenciais

**Nunca** versione a senha do broker. Use o `secrets.h` (já no `.gitignore`):

```cpp
// secrets.h  (criar localmente a partir de secrets_example.h)
#define MQTT_PASS "sua_senha_aqui"
```

## 6. Uso de IA

Conforme as regras estabelecidas pela disciplina, ferramentas de Inteligência Artificial foram utilizadas de forma consciente e responsável como apoio ao desenvolvimento do projeto, auxiliando na pesquisa, compreensão de conceitos e resolução de dúvidas, sem substituir o processo de aprendizagem e implementação realizado pela equipe.

## 7. Licença

Uso acadêmico - INATEL, 2026.
