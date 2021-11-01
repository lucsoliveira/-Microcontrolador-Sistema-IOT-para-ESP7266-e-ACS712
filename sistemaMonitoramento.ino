/*
    Projeto: TCC - Smart Home
    Autor: Lucas de Oliveira Pereira
    Nome: Arquivo .ino referente ao sistema presente no microcontrolador ESP8266
    Descrição: código fonte do hardware referente ao sistema de controle e monitoramento elaborado para o trabalho de 
    conclusão de curso de Engenharia Eletrônica para a Universidade Tecnológica Federal do Paraná
    Campus Campo Mourão.

*/

#include <FS.h>
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  //https://github.com/lucsoliveira/WifiManager-PT-BR-ESP8266-
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient
#include <EEPROM.h>
#include <Filters.h> //Necessária para o calculo RMS dos valores ADC

#define DEBUG                 //Se descomentar esta linha vai habilitar a 'impressão' na porta serial
#define uidDispositivo "SH01" //Tópico SUB MQTT

//MQTT
#define servidor_mqtt "192.168.0.187" //URL do servidor MQTT
#define servidor_mqtt_porta "21494"   //Porta do servidor (a mesma deve ser informada na variável abaixo)
#define servidor_mqtt_usuario "tcc2"  //Usuário
#define servidor_mqtt_senha "tcc2"    //Senha

#define mqtt_topico_sub "SH01/pincmd"    //Tópico SUB MQTT
#define mqtt_topico_pub_sync "SH01/sync" //Tópico PUB SYNC MQTT
#define mqtt_topico_pub "SH01/corrente"  //Tópico PUB MQTT

//Declaração do pino que será utilizado e a memória alocada para armazenar o status deste pino na EEPROM
#define memoria_alocada 4 //Define o quanto sera alocado na EEPROM

//Declaração dos pinos ADC e Saida Digital
#define pino_acionamento 4 //Pino que executara a acao dado no topico SUB

/* Sensor de Corrente */
#define ACS_Pin A0 //Sensor data pin on A0 analog input

float ACS_Value; //Here we keep the raw data valuess
int tensaoTeste = 127;

float frequenciaTeste = 60;
float windowLength = 40.0 / frequenciaTeste; // how long to average the signal, for statistist
float ruido = 0.065;                         // ajustado sem carga
float calibracao = 0.02243902439024390244;    //obtido em comparacao com o ferro de solda e o amperimetro, outro numero com bons resultados: 0.0215726458850178

//Veirifar a que melhor funcione no dispositivo:
//c1 = 0.0215726458850178
//c2 = 0.02243902439024390244
//c3 = 0,02356818181818181818
//c4 = 0,02556818181818181818

float Amps_TRMS; // estimativa da corrente atual
float carga;     //em Ohms

unsigned long startMillis;          //some global variables available anywhere in the program
unsigned long startMillisEnvioMQTT; //some global variables available anywhere in the program

unsigned long currentMillis, currentMillisEnvioMQTT;
const unsigned long period = 1000;   //the value is a number of milliseconds
unsigned long tempoDeAmostra = 1000; // em milliseconds
unsigned long anteriorMillis = 0;
RunningStatistics inputStats;
/* Sensor de Amostra */

/* Instâncias Wifi e MQTT */
WiFiClient espClient;
PubSubClient client(espClient);
/* Fim Insntâncias */

/* Outras configurações */
uint8_t statusAnt = 0;      //Variável que armazenará o status do pino que foi gravado anteriormente na EEPROM
bool precisaSalvar = false; //Flag para salvar os dados
const int delayPublicacao = 1000;

/* Fim outras configurações */

//Função para imprimir na porta serial
void imprimirSerial(bool linha, String mensagem)
{
#ifdef DEBUG
  if (linha)
  {
    Serial.println(mensagem);
  }
  else
  {
    Serial.print(mensagem);
  }
#endif
}

//Função de retorno para notificar sobre a necessidade de salvar as configurações
void precisaSalvarCallback()
{
  imprimirSerial(true, "As configuracoes tem que ser salvas.");
  precisaSalvar = true;
}

//Função que reconecta ao servidor MQTT
void reconectar()
{
  //Repete até conectar
  while (!client.connected())
  {
    imprimirSerial(false, "Tentando conectar ao servidor MQTT...");
    bool conectado = strlen(servidor_mqtt_usuario) > 0 ? client.connect("SH01Client", servidor_mqtt_usuario, servidor_mqtt_senha) : client.connect("SH01Client");
    if (conectado)
    {
      imprimirSerial(true, "Conectado!");
      //Subscreve para monitorar os comandos recebidos
      client.subscribe(mqtt_topico_sub, 1); //QoS 1
    }
    else
    {
      imprimirSerial(false, "Falhou ao tentar conectar. Codigo: ");
      imprimirSerial(false, String(client.state()).c_str());
      imprimirSerial(true, " tentando novamente em 2 segundos");
      //Aguarda 2 segundos para tentar novamente
      delay(2000);
    }
  }
}

//Função que verifica qual foi o último status do pino antes do ESP ser desligado
void lerStatusAnteriorPino()
{
  EEPROM.begin(memoria_alocada); //Aloca o espaco definido na memoria
  statusAnt = EEPROM.read(0);    //Le o valor armazenado na EEPROM e passa para a variável "statusAnt"
  if (statusAnt > 1)
  {
    statusAnt = 0; //Provavelmente é a primeira leitura da EEPROM, passando o valor padrão para o pino.
    EEPROM.write(0, statusAnt);
  }
  digitalWrite(pino_acionamento, statusAnt);
  EEPROM.end();
}

//Função que grava status do pino na EEPROM
void gravarStatusPino(uint8_t statusPino)
{
  EEPROM.begin(memoria_alocada);
  EEPROM.write(0, statusPino);
  EEPROM.end();
}

//Função que será chamada ao receber mensagem do servidor MQTT
void retorno(char *topico, byte *mensagem, unsigned int tamanho)
{
  //Convertendo a mensagem recebida para string
  mensagem[tamanho] = '\0';
  String strMensagem = String((char *)mensagem);
  strMensagem.toLowerCase();
  //float f = s.toFloat();

  imprimirSerial(false, "Mensagem recebida! Topico: ");
  imprimirSerial(false, topico);
  imprimirSerial(false, ". Tamanho: ");
  imprimirSerial(false, String(tamanho).c_str());
  imprimirSerial(false, ". Mensagem: ");
  imprimirSerial(true, strMensagem);

  //Executando o comando solicitado
  imprimirSerial(false, "Status do pino antes de processar o comando: ");
  imprimirSerial(true, String(digitalRead(pino_acionamento)).c_str());

  if (strMensagem == "liga")
  {

    imprimirSerial(true, "Colocando o pino em stado ALTO...");
    digitalWrite(pino_acionamento, HIGH);
    gravarStatusPino(HIGH);
  }

  if (strMensagem == "desliga")
  {
    imprimirSerial(true, "Colocando o pino em stado BAIXO...");
    digitalWrite(pino_acionamento, LOW);
    gravarStatusPino(LOW);
  }

  imprimirSerial(false, "Status do pino depois de processar o comando: ");
  imprimirSerial(true, String(digitalRead(pino_acionamento)).c_str());

  //chama a API de update Sync
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject &JSONencoder = JSONbuffer.createObject();
  JSONencoder["uidDispositivo"] = uidDispositivo;
  JSONencoder["sync"] = 1;

  //client.publish(mqtt_topico_pub, s);

  char JSONmessageBuffer[100];
  JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));

  if (client.publish(mqtt_topico_pub_sync, JSONmessageBuffer) == true)
  {
    Serial.println("Publicação postada no tópico.");
  }
  else
  {
    Serial.println("Erro ao enviar mensagem.");
  }

  delay(50);
}

void setup()
{

#ifdef DEBUG
  Serial.begin(9600);
#endif

  imprimirSerial(true, "...");

  //Formatando a memória interna
  //(descomente a linha abaixo enquanto estiver testando e comente ou apague quando estiver pronto)
  //SPIFFS.format();

  pinMode(pino_acionamento, OUTPUT);
  pinMode(ACS_Pin, INPUT); //Define the pin mode

  Serial.begin(115200);
  delay(10);

  Serial.println(F("Init...."));

  //Iniciando o SPIFSS (SPI Flash File System)
  imprimirSerial(true, "Iniciando o SPIFSS (SPI Flash File System)");
  if (SPIFFS.begin())
  {
    imprimirSerial(true, "Sistema de arquivos SPIFSS montado!");
    if (SPIFFS.exists("/config.json"))
    {
      //Arquivo de configuração existe e será lido.
      imprimirSerial(true, "Abrindo o arquivo de configuracao...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        imprimirSerial(true, "Arquivo de configuracao aberto.");
        size_t size = configFile.size();

        //Alocando um buffer para armazenar o conteúdo do arquivo.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
          //Copiando as variáveis salvas previamente no aquivo json para a memória do ESP.
          imprimirSerial(true, "arquivo json analisado.");
          strcpy(servidor_mqtt, json["servidor_mqtt"]);
          strcpy(servidor_mqtt_porta, json["servidor_mqtt_porta"]);
          strcpy(servidor_mqtt_usuario, json["servidor_mqtt_usuario"]);
          strcpy(servidor_mqtt_senha, json["servidor_mqtt_senha"]);
          strcpy(mqtt_topico_sub, json["mqtt_topico_sub"]);
        }
        else
        {
          imprimirSerial(true, "Falha ao ler as configuracoes do arquivo json.");
        }
      }
    }
  }
  else
  {
    imprimirSerial(true, "Falha ao montar o sistema de arquivos SPIFSS.");
  }
  /* FIM SPIFSS */

  //Parâmetros para configuração do MQTT
  WiFiManagerParameter custom_mqtt_server("server", "Servidor MQTT", servidor_mqtt, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Porta", servidor_mqtt_porta, 6);
  WiFiManagerParameter custom_mqtt_user("user", "Usuario", servidor_mqtt_usuario, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "Senha", servidor_mqtt_senha, 20);
  WiFiManagerParameter custom_mqtt_topic_sub("topic_sub", "Topico para subscrever", mqtt_topico_sub, 30);

  WiFiManager wifiManager;

  //Descomente as linhas abaixo caso seja necessário resetar as configurações de Rede do ESP8266
  /*
  wifiManager.resetSettings();
  ESP.eraseConfig();
  */

  //Definindo a função que informará a necessidade de salvar as configurações
  wifiManager.setSaveConfigCallback(precisaSalvarCallback);

  //Adicionando os parâmetros para conectar ao servidor MQTT
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_topic_sub);

  /* 
   Busca a wifi salva na memória. Se não conectar, cria o AP.
   */

  if (!wifiManager.autoConnect("AutoConnectAP", "senha123"))
  {
    imprimirSerial(true, "Falha ao conectar. Excedeu o tempo limite para conexao.");
    delay(3000);

    //Reinicia o ESP e tenta novamente ou entra em sono profundo (DeepSleep)
    ESP.reset();
    delay(5000);
  }

  imprimirSerial(true, "Conectado!! :)");

  //Lendo os parâmetros atualizados
  strcpy(servidor_mqtt, custom_mqtt_server.getValue());
  strcpy(servidor_mqtt_porta, custom_mqtt_port.getValue());
  strcpy(servidor_mqtt_usuario, custom_mqtt_user.getValue());
  strcpy(servidor_mqtt_senha, custom_mqtt_pass.getValue());
  strcpy(mqtt_topico_sub, custom_mqtt_topic_sub.getValue());

  //Salvando os parâmetros informados na tela web do WiFiManager
  if (precisaSalvar)
  {
    imprimirSerial(true, "Salvando as configuracoes");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();
    json["servidor_mqtt"] = servidor_mqtt;
    json["servidor_mqtt_porta"] = servidor_mqtt_porta;
    json["servidor_mqtt_usuario"] = servidor_mqtt_usuario;
    json["servidor_mqtt_senha"] = servidor_mqtt_senha;
    json["mqtt_topico_sub"] = mqtt_topico_sub;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      imprimirSerial(true, "Houve uma falha ao abrir o arquivo de configuracao para incluir/alterar as configuracoes.");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }

  imprimirSerial(false, "IP: ");
  imprimirSerial(true, WiFi.localIP().toString());

  //Informando ao client do PubSub a url do servidor e a porta.
  int portaInt = atoi(servidor_mqtt_porta);
  client.setServer(servidor_mqtt, portaInt);
  client.setCallback(retorno);

  //Obtendo o status do pino antes do ESP ser desligado
  lerStatusAnteriorPino();

  startMillis = millis();                 //initial start time
  startMillisEnvioMQTT = millis();        //initial start time
  inputStats.setWindowSecs(windowLength); //Set the window length
}

void loop()
{

  if (!client.connected())
  {
    reconectar();
  }
  else
  {

    client.loop();

    /* Cálculo da corrente */

    ACS_Value = analogRead(ACS_Pin); // read the analog in value:
    currentMillis = millis();        //get the current "time" (actually the number of milliseconds since the program started)

    inputStats.input(ACS_Value); // log to Stats function
        //Serial.println("entrou aqui");

    if (currentMillis - startMillis >= period) //test whether the period has elapsed
    {
      Amps_TRMS = calibracao * inputStats.sigma();

      if (Amps_TRMS <= ruido)
      {
        Amps_TRMS = 0;
      }

      if (Amps_TRMS > 0)
      {
        carga = tensaoTeste / Amps_TRMS;
      }
      else
      {
        carga = 0;
      }

      //print console
      Serial.print("Sem calibracao: ");
      Serial.print(inputStats.sigma());
      Serial.print(" | Amps : ");
      Serial.print(Amps_TRMS);
      Serial.print(" | Carga (Ohms): ");
      Serial.println(carga);

      delay(10);

      char s[7];
      sprintf(s, "%f", Amps_TRMS);

      StaticJsonBuffer<300> JSONbuffer;
      JsonObject &JSONencoder = JSONbuffer.createObject();

      JSONencoder["uidDispositivo"] = uidDispositivo;
      JSONencoder["corrente"] = Amps_TRMS;
      if (Amps_TRMS != 0)
      {
        JSONencoder["carga"] = carga; //verificar se o AmpRMS está correto
      }
      else
      {
        JSONencoder["carga"] = 0;
      }

      char JSONmessageBuffer[100];
      JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));

      Serial.println("Enviando mensagem  para o topico MQTT...");
      Serial.println(JSONmessageBuffer);
      client.publish(mqtt_topico_pub, JSONmessageBuffer);
      //fim envio ao servidor

      delay(50);
      startMillis = currentMillis; //IMPORTANT to save the start time of the current LED state.
    }
  }
}
