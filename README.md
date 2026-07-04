# Dosador Inteligente de Medicamentos com IoT e ESP32

Projeto desenvolvido para monitoramento térmico, controle volumétrico de dosagem e bloqueio preventivo de segurança utilizando conceitos de Internet das Coisas (IoT).

## 🚀 Tecnologias Utilizadas
* **Hardware:** ESP32, Motor de Passo (28BYJ-48), Driver ULN2003A, Micro Servo Motor SG90, Display OLED I2C, Buzzer e LEDs.
* **Backend & Infraestrutura:** Docker, Node-RED, Banco de Dados PostgreSQL e Protocolo MQTT.
* **Frontend/Supervisório:** Grafana.

## 🛠️ Como Executar o Projeto

1. Clone este repositório.
2. Certifique-se de ter o **Docker Desktop** instalado.
3. No terminal da pasta raiz, execute o comando para subir os serviços:
   ```bash
   docker compose up -d
