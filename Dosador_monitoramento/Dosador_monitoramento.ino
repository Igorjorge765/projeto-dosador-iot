#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <Stepper.h>
#include "BluetoothSerial.h" 

BluetoothSerial SerialBT;

// --- CONFIGURAÇÃO DO MQTT ---
const char* mqtt_server = "192.168.68.105"; //mude de acordo com seu ip e faça a mesma coisa no fluxo do node-red
const int mqtt_port = 1883;
const char* topico_publicar = "dosador/dados";

WiFiClient espClient;
PubSubClient client(espClient);

// --- CONFIGURAÇÃO DO OLED ---
#define LARGURA_TELA 128 
#define ALTURA_TELA 64 
#define OLED_RESET     -1 
Adafruit_SSD1306 display(LARGURA_TELA, ALTURA_TELA, &Wire, OLED_RESET);

// --- PINOS FÍSICOS ---
const int PIN_POT = 35;          
const int PIN_NTC = 34;          
const int PIN_BOTAO = 4;         
const int PIN_LED_VERDE = 2;     
const int PIN_LED_VERMELHO = 15; 
const int PIN_BUZZER = 27;       
const int PIN_SERVO = 26;
const int STEPS_PER_REVOLUTION = 2048; 

Stepper meuMotorDePasso(STEPS_PER_REVOLUTION, 13, 14, 12, 25);
Servo meuServo;

const float TEMP_LIMITE = 24.0;
const float SERIES_RESISTOR = 10000.0; 
const float NOMINAL_RESISTANCE = 10000.0;
const float NOMINAL_TEMPERATURE = 25.0;
const float B_COEFFICIENT = 3950.0; 

//--- Variaveis auxiliares
bool sistemaBloqueado = false;
int doseManualCelular = -1; 
bool modoSimulacao = false; // Controla se estamos em modo de teste ou real
int doseML = 0;             
float temperatura_atual = 0.0; 

// Temporizadores independentes para não travar o loop
unsigned long ultimoEnvioMQTT = 0;
unsigned long tempoUltimoEnvio = 0;
const long intervaloEnvio = 5000; 
unsigned long ultimoPrintSerial = 0;
const long intervaloPrint = 3000; // Printa dados no terminal a cada 3 segundos

bool flagMqttConectadoAnt = false;
bool flagBtConectadoAnt = false;

// Declaração de funções
void verificarConexaoMQTT();
void processarBluetooth(); 
float lerTemperaturaC();
void seguranca();
void enviarDadosMQTT(int dosagem, float temp, String statusAtual);
void aplicarDose(int dosagem, float temp);

void setup()
{
  Serial.begin(115200);
  delay(500);
  
  // 1. Inicializa o Bluetooth e avisa apenas uma vez
  SerialBT.begin("Dosador_Remoto"); 
  Serial.println(F("\n[SISTEMA] Bluetooth inicializado! Aguardando conexao do celular..."));

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT); 

  meuMotorDePasso.setSpeed(12); 
  ESP32PWM::allocateTimer(0);
  meuServo.setPeriodHertz(50);
  meuServo.attach(PIN_SERVO, 500, 2400); 
  meuServo.write(0); 

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println(F("Conectando Wi-Fi..."));
  display.display();

  // 2. WiFiManager: Pede a rede e conecta de forma obrigatória aqui
  WiFiManager wm;
  Serial.println(F("[SISTEMA] Iniciando verizacao de rede Wi-Fi..."));
  if(!wm.autoConnect("ESP32_Config")) {
    Serial.println(F("[ERRO] Falha no Wi-Fi. Reiniciando..."));
    ESP.restart(); 
  }
  Serial.print(F("[SISTEMA] Wi-Fi Conectado! IP obtido: "));
  Serial.println(WiFi.localIP());

  // Configura o servidor, e avisa que vai tentar em background
  client.setServer(mqtt_server, mqtt_port); 
  Serial.println(F("[SISTEMA] Tentando primeira conexao ao MQTT..."));
}

void loop()
{
  // --- 1. AÇÕES DO HARDWARE IMEDIATAS (Sem delays secundários) ---
  if(digitalRead(PIN_BOTAO) == LOW) {
    aplicarDose(doseML, temperatura_atual);
  }

  // --- 2. LEITURAS CONSTANTES ---
  if (doseManualCelular == -1) {
    int valor_bruto = analogRead(PIN_POT);
    doseML = map(valor_bruto, 0, 4095, 0, 50); 
  } else {
    doseML = doseManualCelular; 
  }
  if (modoSimulacao) 
  {
  // Para ir de 15.0 a 45.0:
  temperatura_atual= random(150, 451) / 10.0;
  } 
else 
  {
  // Sua leitura original do sensor físico aqui
  temperatura_atual = lerTemperaturaC();
  }

  seguranca();

  // --- 3. COMANDOS DO BLUETOOTH DO CELULAR ---
  processarBluetooth();

  // --- 4. TENTAR CONECTAR MQTT (Fundo de tela/Não bloqueante) ---
  verificarConexaoMQTT();

  // --- 5. DETECÇÃO DE STATUS DO BLUETOOTH NO SERIAL ---
  bool btConectadoAgora = SerialBT.hasClient();
  if (btConectadoAgora && !flagBtConectadoAnt) {
    Serial.println(F("\n>>> [NOTIFICACAO] Celular CONECTOU via Bluetooth! <<<"));
    flagBtConectadoAnt = true;
  } else if (!btConectadoAgora && flagBtConectadoAnt) {
    Serial.println(F("\n>>> [NOTIFICACAO] Celular DESCONECTOU do Bluetooth. <<<"));
    flagBtConectadoAnt = false;
  }

  // --- 6. MOSTRAR DADOS ENVIADOS NO SERIAL DO PC (A cada 3 segundos) ---
  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimoPrintSerial >= intervaloPrint) {
    ultimoPrintSerial = tempoAtual;
    Serial.print(F("[DADOS ENVIADOS] Temp: ")); 
    Serial.print(temperatura_atual, 1);
    Serial.print(F(" C | Dosagem Atual: ")); 
    Serial.print(doseML);
    Serial.print(F(" ml | MQTT: ")); 
    Serial.print(client.connected() ? F("CONECTADO") : F("DESCONECTADO"));
    Serial.print(F(" | Estado: ")); Serial.println(sistemaBloqueado ? F("BLOQUEADO") : F("MONITORAMENTO"));
  }

  // --- 7. ENVIO PERIÓDICO MQTT PARA O DOCKER ---
  if (tempoAtual - ultimoEnvioMQTT >= intervaloEnvio) {
    ultimoEnvioMQTT = tempoAtual;
    if(client.connected()) {
      enviarDadosMQTT(doseML, temperatura_atual, "MONITORAMENTO");
    }
  }

  // --- 8. ATUALIZAR OLED DISPLAY ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Conferencia de Doses"));
  display.println(F("====================="));
  display.print(F("Temp: ")); display.print(temperatura_atual, 1); display.println(F(" C"));
  if (doseManualCelular == -1) {
    display.print(F("Dose (Pot): ")); display.print(doseML); display.println(F(" ml"));
  } else {
    display.print(F("Dose (BT):  ")); display.print(doseML); display.println(F(" ml *"));
  }
  display.println(F("---------------------"));
  display.print(F("STATUS: ")); display.println(sistemaBloqueado ? F("BLOQUEADO") : F("SEGURO"));
  display.display();

  delay(10);
}

void verificarConexaoMQTT() {
  if (client.connected()) {
    client.loop();
    if (!flagMqttConectadoAnt) {
      Serial.println(F("\n>>> [NOTIFICACAO] Broker MQTT Conectado com sucesso! <<<"));
      flagMqttConectadoAnt = true;
    }
    return;
  }

  if (flagMqttConectadoAnt) {
    Serial.println(F("\n>>> [AVISO] Falha ou perda de conexao com MQTT. Retrying... <<<"));
    flagMqttConectadoAnt = false;
  }

  // Tenta reconexão rápida a cada 10 segundos, mas sem parar a execução do loop
  static unsigned long ultimaTentativaMQTT = 0;
  if (millis() - ultimaTentativaMQTT > 10000) { 
    ultimaTentativaMQTT = millis();
    if (WiFi.status() == WL_CONNECTED) {
      String client_id = "ESP32-" + String(random(0xffff), HEX);
      client.connect(client_id.c_str());
    }
  }
}
//Função de comandos por bluetooth
void processarBluetooth() 
{ 
  //
  if (SerialBT.available()) {
    String comando = SerialBT.readString();
    comando.trim(); 

    Serial.print(F("\n[COMANDO RECEBIDO BT]: ["));
    Serial.print(comando);
    Serial.println(F("]"));

    //aplicar a dose 
    if (comando.equalsIgnoreCase("D")) {
      aplicarDose(doseML, temperatura_atual);
    } 
    //mostra os status temperatura e dose atual medida no potenciometro
    else if (comando.equalsIgnoreCase("S")) {
      SerialBT.print("Temp: "); SerialBT.print(temperatura_atual, 1);
      SerialBT.print("C | Dose: "); SerialBT.print(doseML); SerialBT.println("ml");
    }
    //comando para caso esteja no modo de bluetooth volta para ver as doses pelo potenciometro
    else if (comando.equalsIgnoreCase("P")) {
      doseManualCelular = -1;
      SerialBT.println("Modo Potenciometro Ativo");
    }
    //comando para simular uma temperatura , util quando a temperatura da sala faz o NTC ter um valor muito baixo
    else if(comando.equalsIgnoreCase("Simular"))
    {
      modoSimulacao = true;
      Serial.println("Modo Simulação Ativado!");
    }
    //comando para voltar a ver a temperatura real
    else if(comando.equalsIgnoreCase("Real"))
    {
      modoSimulacao = false;
      Serial.println("Modo Real Ativado!");  
    }
    else 
    {
      //comando para trocar o valor da dose do potenciometro , esse comando troca o modo de ver a dose
      int valorRecebido = comando.toInt();
      if (valorRecebido > 0 && valorRecebido <= 50) {
        doseManualCelular = valorRecebido;
        SerialBT.print("Dose BT Configurada: "); SerialBT.print(doseManualCelular); SerialBT.println("ml");
      }
    }
  }
}

float lerTemperaturaC() {
  int valorBrutoNTC = analogRead(PIN_NTC);
  if (valorBrutoNTC < 10) return -273.15;
  float resistencia = SERIES_RESISTOR * ((4095.0 / valorBrutoNTC) - 1.0);
  float temperaturaK = resistencia / NOMINAL_RESISTANCE;     
  temperaturaK = log(temperaturaK);                    
  temperaturaK /= B_COEFFICIENT;                       
  temperaturaK += 1.0 / (NOMINAL_TEMPERATURE + 273.15); 
  temperaturaK = 1.0 / temperaturaK;                    
  return (temperaturaK - 273.15);
}

void seguranca() {
  if(temperatura_atual >= TEMP_LIMITE) {
    digitalWrite(PIN_LED_VERMELHO, HIGH);
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_BUZZER, HIGH); 
    sistemaBloqueado = true;
  } else {
    digitalWrite(PIN_LED_VERMELHO, LOW);
    digitalWrite(PIN_LED_VERDE, HIGH);
    digitalWrite(PIN_BUZZER, LOW); 
    sistemaBloqueado = false;
  }  
}

void enviarDadosMQTT(int dosagem, float temp, String statusAtual) {
  String jsonPayload = "{\"temperatura\":" + String(temp, 1) + 
                       ",\"dosagem_ml\":" + String(dosagem) + 
                       ",\"status\":\"" + statusAtual + "\"}";
  client.publish(topico_publicar, jsonPayload.c_str());
}

void aplicarDose(int dosagem, float temp) {
  if(sistemaBloqueado || dosagem == 0) return;

  display.clearDisplay();
  display.setCursor(0, 10);
  display.println(F("   INJETANDO DOSE   "));
  display.print(F("\n Aplicando: ")); display.print(dosagem); display.println(F(" ml"));
  display.display();

  digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW);

  meuServo.write(90);
  delay(600); 
  meuMotorDePasso.step(dosagem * 100); 
  meuServo.write(0);
  delay(400);
  
  if(client.connected()) {
    enviarDadosMQTT(dosagem, temp, "DOSE_CONCLUIDA");
    Serial.print(F("[DADOS ENVIADOS] Temp: ")); 
    Serial.print(temperatura_atual, 1);
    Serial.print(F(" C | Dosagem Atual: ")); 
    Serial.print(doseML);
    Serial.print(F(" ml | MQTT: ")); 
    Serial.print(client.connected() ? F("CONECTADO") : F("DESCONECTADO"));
    Serial.print(F(" | Estado: ")); Serial.println(sistemaBloqueado ? F("BLOQUEADO") : F("DOSE_CONCLUIDA"));
  }
  delay(500);
}
