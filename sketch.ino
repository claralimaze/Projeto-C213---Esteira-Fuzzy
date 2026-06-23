// ================================================================
//  PROJETO FINAL C213 - Esteira Inteligente com Controle Fuzzy
//  Versao CONSOLIDADA (reaproveita o mapeamento de pinos do grupo)
//  Grupo: [NOME 1] / [NOME 2] / [NOME 3]   -  INATEL 2025
// ================================================================
//  ESTE ARQUIVO UNIFICA:
//   - a base do grupo (servo no D18, LEDs 25/26/27, pot D34, I2C 21/22)
//   - + buzzer, HC-SR04 e botao de emergencia (novos, em pinos livres)
//   - + controlador FUZZY REAL (pertinencia + centroide)
//   - + alarme sonoro no buzzer  (corrige o ponto 1)
//   - + travamento so apos 4 min  (corrige o ponto 2)
//   - + MQTT/HiveMQ p/ dashboard Node-RED (entregavel obrigatorio, 15 pts)
// ----------------------------------------------------------------
//  DISPLAY: este codigo usa LCD I2C 16x2 (como na sua foto).
//  Se voces usarem o OLED SSD1306, ver o bloco comentado "OPCAO OLED".
//  SEGURANCA: nao versionar a senha. Use secrets.h no .gitignore.
// ================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// ───────────── PINOS (reaproveitados + novos) ─────────────
#define POT_PIN        34   // (reuso) potenciometro -> carga
#define MOTOR_PIN      18   // (reuso) atuador: servo OU driver de motor CC
#define LED_VERDE      25   // (reuso)
#define LED_AMARELO    26   // (reuso)
#define LED_VERMELHO   27   // (reuso)
// I2C do display: SDA=21, SCL=22 (reuso)
#define BUZZER_PIN     32   // (NOVO) alarme sonoro
#define TRIG_PIN        5   // (NOVO) HC-SR04
#define ECHO_PIN       23   // (NOVO) HC-SR04
#define BTN_EMERG      33   // (NOVO) botao de emergencia (pull-up)

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo motor;   // atuador (visual no Wokwi). Em HW real: motor CC + driver.

// ───────────── WiFi / MQTT (HiveMQ) ─────────────
const char* ssid      = "Wokwi-GUEST";
const char* password  = "";
const char* mqtt_host = "b6c228153f69435486ef56fc32db30fc.s1.eu.hivemq.cloud";
const int   mqtt_port = 8883;
const char* mqtt_user = "Projeto-C213";
const char* mqtt_pass = "SUA_SENHA_MQTT";   // <- preencher localmente

const char* TOP_RPM="esteira/rpm", *TOP_PWM="esteira/pwm", *TOP_ERRO="esteira/erro",
           *TOP_CARGA="esteira/carga", *TOP_SP="esteira/setpoint", *TOP_PRODUTOS="esteira/produtos",
           *TOP_TAMANHO="esteira/tamanho", *TOP_THRU="esteira/throughput", *TOP_STATUS="esteira/status",
           *TOP_ALERTA="esteira/alerta", *TOP_CMD_SP="esteira/cmd/setpoint",
           *TOP_CMD_LOAD="esteira/cmd/carga", *TOP_CMD_EMERG="esteira/cmd/emergencia";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// ───────────── PLANTA (mesmos parametros do grupo) ─────────────
const float K=1.2, B=0.08, C=0.5, dt=0.1;

// ───────────── VARIAVEIS ─────────────
float setpoint=500, carga=0, rpm=0, pwm=30, erro=0, erroAnt=0, deltaErro=0;
bool  emergencia=false;
int   contadorProdutos=0;  float ultimoTamanho=0;  bool produtoPresente=false;
unsigned long tProdutos[64]; int idxProd=0;
unsigned long tBloqueioInicio=0;   // marca quando o sensor comecou a ficar bloqueado

// >>> PONTO 2: travamento so apos 4 minutos de bloqueio continuo <<<
const unsigned long JAM_TIMEOUT_MS = 4UL*60UL*1000UL;   // 4 minutos

// >>> CONTAGEM HIBRIDA: automatica por tempo + sensor manual <<<
bool  modoAuto = true;                     // liga a contagem automatica
const unsigned long INTERVALO_AUTO = 3000; // uma caixa a cada 3 s (ajustavel)
unsigned long tProxAuto = 0;               // proximo instante de contagem auto
String classeTam = "-";                    // Pequeno / Medio / Grande

// >>> CARGA ATRELADA AO PRODUTO <<<
// Cada caixa que entra soma um peso conforme o tamanho (P/M/G); essa parcela
// decai com o tempo (caixas saindo da esteira). O potenciometro continua
// valendo como carga-base (sempre ativo) e e' somado a essa parcela.
float cargaProdutos = 0.0f;                // parcela de carga vinda das caixas
const float PESO_P = 3.0f, PESO_M = 6.0f, PESO_G = 10.0f;  // kg por classe
const float DECAIMENTO = 0.3f;             // kg/ciclo que "sai" da esteira

// >>> BUZZER: sirene de dois tons <<<
const int TOM_A = 880, TOM_B = 1320;       // Hz alternados

// ================================================================
//  CONTROLADOR FUZZY REAL  (pertinencia + min + centroide)
//  -> corrige o ponto 4: agora ha grau de pertinencia e interpolacao
// ================================================================
float tri(float x,float a,float b,float c){
  if(x<=a||x>=c) return 0; if(x==b) return 1;
  return (x<b)?(x-a)/(b-a):(c-x)/(c-b);
}
float trapEsq(float x,float b,float c){ if(x<=b)return 1; if(x>=c)return 0; return (c-x)/(c-b);}
float trapDir(float x,float b,float c){ if(x>=c)return 1; if(x<=b)return 0; return (x-b)/(c-b);}

// Erro (universo +-500)
float eNG(float e){return trapEsq(e,-400,-200);} float eNP(float e){return tri(e,-400,-200,0);}
float eZ (float e){return tri(e,-100,0,100);}     float ePP(float e){return tri(e,0,200,400);}
float ePG(float e){return trapDir(e,200,400);}
// dErro (universo estreito +-40 -> acao derivativa antecipa frenagem)
float dNG(float d){return trapEsq(d,-32,-16);} float dNP(float d){return tri(d,-32,-16,0);}
float dZ (float d){return tri(d,-8,0,8);}        float dPP(float d){return tri(d,0,16,32);}
float dPG(float d){return trapDir(d,16,32);}

// Base de regras (singletons de saida: NG=-30 NP=-15 Z=0 PP=15 PG=30)
const float SAIDA[5][5]={
  {-30,-30,-30,-15,  0},
  {-30,-15,-15,  0, 15},
  {-15,-15,  0, 15, 15},
  {-15,  0, 15, 15, 30},
  {  0, 15, 30, 30, 30}};

float fuzzy(float e,float de){
  float mE[5]={eNG(e),eNP(e),eZ(e),ePP(e),ePG(e)};
  float mD[5]={dNG(de),dNP(de),dZ(de),dPP(de),dPG(de)};
  float num=0,den=0;
  for(int i=0;i<5;i++)for(int j=0;j<5;j++){
    float w=min(mE[i],mD[j]);              // inferencia (AND = min)
    if(w>0){ num+=w*SAIDA[i][j]; den+=w; } // centroide (singletons)
  }
  return (den==0)?0:num/den;
}

// ================================================================
//  PLANTA / LEITURAS
// ================================================================
void calcularPlanta(){ rpm += dt*(K*pwm - B*rpm - C*carga); rpm=constrain(rpm,0,1000); }

// CARGA = carga-base do potenciometro (SEMPRE ativo) + parcela dos produtos.
// A parcela de produtos decai a cada ciclo (caixas saindo da esteira).
void lerCarga(){
  float base = analogRead(POT_PIN)/4095.0f*40.0f;   // potenciometro sempre lido
  cargaProdutos = max(0.0f, cargaProdutos - DECAIMENTO);
  carga = constrain(base + cargaProdutos, 0.0f, 40.0f);
}

const float LIMIAR=15.0, ALT_SENSOR=20.0;   // cm
float lerUltrassom(){
  digitalWrite(TRIG_PIN,LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH); delayMicroseconds(10); digitalWrite(TRIG_PIN,LOW);
  long dur=pulseIn(ECHO_PIN,HIGH,30000); if(dur==0) return 999;
  return dur*0.0343/2.0;
}
// Classifica e registra um produto. Cada caixa SOMA um peso a' carga,
// amarrando a contagem ao peso conforme o tamanho (P/M/G).
void classificar(float tam){
  if(tam<6)       classeTam="Pequeno";
  else if(tam<13) classeTam="Medio";
  else            classeTam="Grande";
}
void registrarProduto(float tam){
  contadorProdutos++;
  ultimoTamanho=tam;
  classificar(tam);
  if(classeTam=="Pequeno")    cargaProdutos += PESO_P;
  else if(classeTam=="Medio") cargaProdutos += PESO_M;
  else                        cargaProdutos += PESO_G;
  cargaProdutos = min(cargaProdutos, 40.0f);
  tProdutos[idxProd%64]=millis(); idxProd++;
}

// (a) Contagem AUTOMATICA por tempo, com tamanho aleatorio P/M/G.
//     So conta com a esteira EM MOVIMENTO (rpm>50) -> resolve o caso de
//     "continuar contando com carga/velocidade zero".
void contagemAutomatica(){
  if(!modoAuto || emergencia || rpm < 50.0f) return;
  if(millis()>=tProxAuto){
    float t[3]={4.0,9.0,16.0};            // alturas exemplo: Peq, Med, Gra
    registrarProduto(t[random(0,3)]);
    tProxAuto=millis()+INTERVALO_AUTO;
  }
}

// (b) Contagem MANUAL pelo HC-SR04 (continua funcionando)
void processarProduto(){
  float dist=lerUltrassom(); bool det=(dist<LIMIAR);
  if(det && !produtoPresente){          // borda = caixa passando
    registrarProduto(constrain(ALT_SENSOR-dist,0,ALT_SENSOR));
    tBloqueioInicio=millis();           // inicia janela de bloqueio
  }
  if(!det){ tBloqueioInicio=0; }        // liberou: zera timer de bloqueio
  produtoPresente=det;
}
float throughput(){ unsigned long a=millis(); int c=0;
  for(int i=0;i<min(idxProd,64);i++) if(a-tProdutos[i]<=60000UL) c++; return c; }

// ================================================================
//  SUPERVISAO INTELIGENTE (decisao automatica)
//  Ponto 2: a esteira SO sinaliza travamento se a MESMA caixa ficar
//  parada em frente ao sensor por >= 4 min. Caixas passando (mesmo do
//  mesmo tamanho) apenas incrementam a contagem e a esteira segue rodando.
// ================================================================
void supervisao(){
  if(produtoPresente && tBloqueioInicio>0 &&
     (millis()-tBloqueioInicio>=JAM_TIMEOUT_MS) && !emergencia){
    emergencia=true;
    client.publish(TOP_ALERTA,"TRAVAMENTO: caixa parada >4 min. Parada automatica.");
    client.publish(TOP_STATUS,"TRAVAMENTO");
  }
}

// ================================================================
//  ATUACAO
// ================================================================
void atuarMotor(){
  // SERVO (visual no Wokwi): angulo proporcional ao PWM.
  // Em hardware real, recomenda-se MOTOR CC + motorredutor + driver:
  //   trocar por ledcWrite(canal, map(pwm,0,100,0,255));  // duty -> velocidade
  int ang = map((int)pwm,0,100,0,180);
  if(emergencia) ang=0;
  motor.write(ang);
}
void atuarLEDs(){
  if(emergencia){ digitalWrite(LED_VERDE,LOW); digitalWrite(LED_AMARELO,LOW);
                  digitalWrite(LED_VERMELHO,(millis()/300)%2); return; }
  float ae=fabs(erro);
  digitalWrite(LED_VERDE,   ae<20?HIGH:LOW);
  digitalWrite(LED_AMARELO,(ae>=20&&ae<50)?HIGH:LOW);
  digitalWrite(LED_VERMELHO,ae>=50?HIGH:LOW);
}
// >>> PONTO buzzer: sirene de dois tons (chama atencao sem ser ruido fixo) <<<
void atuarBuzzer(){
  if(emergencia) tone(BUZZER_PIN, (millis()/350)%2 ? TOM_B : TOM_A);
  else           noTone(BUZZER_PIN);
}

void atualizarLCD(){
  lcd.setCursor(0,0);
  if(emergencia) lcd.print("** EMERGENCIA ** ");
  else { char l[17]; snprintf(l,sizeof(l),"RPM:%4d SP:%4d",(int)rpm,(int)setpoint); lcd.print(l); }
  lcd.setCursor(0,1);
  char l2[17]; snprintf(l2,sizeof(l2),"PWM:%3d Prod:%3d",(int)pwm,contadorProdutos); lcd.print(l2);
}

void lerBotao(){
  static unsigned long t=0;
  if(digitalRead(BTN_EMERG)==LOW && millis()-t>400){
    emergencia=!emergencia; t=millis();
    if(!emergencia) tBloqueioInicio=0;
    client.publish(TOP_STATUS, emergencia?"EMERGENCIA":"NORMAL");
  }
}

// ================================================================
//  MQTT
// ================================================================
void callback(char* topic, byte* payload, unsigned int len){
  String m=""; for(unsigned int i=0;i<len;i++) m+=(char)payload[i]; String t=topic;
  if(t==TOP_CMD_SP){ float v=m.toFloat(); if(v>=0&&v<=1000) setpoint=v; }
  else if(t==TOP_CMD_LOAD){ float v=m.toFloat(); if(v>=0&&v<=40){cargaProdutos=v;} }
  else if(t==TOP_CMD_EMERG){ emergencia=(m.toInt()==1); if(!emergencia) tBloqueioInicio=0;
    client.publish(TOP_STATUS, emergencia?"EMERGENCIA":"NORMAL"); }
  else if(t=="esteira/cmd/auto"){ modoAuto=(m.toInt()==1); tProxAuto=millis()+INTERVALO_AUTO; }
}
void conectarMQTT(){
  espClient.setInsecure(); client.setServer(mqtt_host,mqtt_port); client.setCallback(callback);
  while(!client.connected()){
    if(client.connect("ESP32_Esteira_C213",mqtt_user,mqtt_pass)){
      client.subscribe(TOP_CMD_SP); client.subscribe(TOP_CMD_LOAD); client.subscribe(TOP_CMD_EMERG);
      client.subscribe("esteira/cmd/auto");
      client.publish(TOP_STATUS,"NORMAL");
    } else delay(3000);
  }
}
void publicar(){
  client.publish(TOP_RPM,String(rpm,1).c_str());   client.publish(TOP_PWM,String(pwm,1).c_str());
  client.publish(TOP_ERRO,String(erro,1).c_str());  client.publish(TOP_CARGA,String(carga,1).c_str());
  client.publish(TOP_SP,String(setpoint,1).c_str());
  client.publish(TOP_PRODUTOS,String(contadorProdutos).c_str());
  client.publish(TOP_TAMANHO,String(ultimoTamanho,1).c_str());
  client.publish("esteira/classe",classeTam.c_str());
  client.publish(TOP_THRU,String(throughput(),0).c_str());
}

// ================================================================
void setup(){
  Serial.begin(115200);
  pinMode(LED_VERDE,OUTPUT); pinMode(LED_AMARELO,OUTPUT); pinMode(LED_VERMELHO,OUTPUT);
  pinMode(BUZZER_PIN,OUTPUT); pinMode(TRIG_PIN,OUTPUT); pinMode(ECHO_PIN,INPUT);
  pinMode(BTN_EMERG,INPUT_PULLUP);
  motor.attach(MOTOR_PIN);
  Wire.begin(21,22); lcd.init(); lcd.backlight();
  lcd.print(" Esteira Fuzzy ");
  WiFi.begin(ssid,password); while(WiFi.status()!=WL_CONNECTED) delay(300);
  conectarMQTT(); lcd.clear();
  randomSeed(analogRead(POT_PIN)+millis());
  tProxAuto=millis()+INTERVALO_AUTO;
}

// ================================================================
void loop(){
  if(!client.connected()) conectarMQTT();
  client.loop();
  lerBotao(); lerCarga(); processarProduto(); contagemAutomatica();

  if(emergencia){ pwm=0; rpm=max(0.0f,rpm-30.0f); erro=setpoint-rpm; }
  else{
    calcularPlanta();
    erroAnt=erro; erro=setpoint-rpm;
    deltaErro=constrain(erro-erroAnt,-40.0f,40.0f);
    pwm=constrain(pwm+fuzzy(erro,deltaErro),0.0f,100.0f);   // FUZZY
    supervisao();
  }
  atuarMotor(); atuarLEDs(); atuarBuzzer(); atualizarLCD(); publicar();
  delay((int)(dt*1000));
}
