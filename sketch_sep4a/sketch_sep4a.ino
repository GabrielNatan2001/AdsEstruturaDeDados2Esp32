#include "EmonLib.h"
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// ===== CONFIG WIFI =====
const char* ssid = "M54teste";
const char* password = "123456789";

// ===== Servidor =====
WebServer server(80);

// ===== LED =====
const int LED_PIN = 2;  // LED embutido do ESP32
bool alertaAtivo = false;

// ===== Limite de consumo (valor inicial = 150W) =====
float limiteConsumo = 150.0;

// ===== Medição de Corrente =====
EnergyMonitor SCT013;
int pinSCT = 34;   //Pino analógico conectado ao SCT-013
int tensao = 127;

// ===== Estrutura da árvore =====
struct Node {
  unsigned long key;
  String timestamp;
  float consumo;
  int maquina;   // número da máquina
  Node* left;
  Node* right;
};

Node* root = NULL; // Árvore de medições

// ===== Funções da árvore =====
Node* newNode(unsigned long key, String timestamp, float consumo, int maquina) {
  Node* node = new Node;
  node->key = key;
  node->timestamp = timestamp;
  node->consumo = consumo;
  node->maquina = maquina;
  node->left = node->right = NULL;
  return node;
}

Node* insertNode(Node* node, unsigned long key, String timestamp, float consumo, int maquina) {
  if (node == NULL) return newNode(key, timestamp, consumo, maquina);
  if (key < node->key) node->left = insertNode(node->left, key, timestamp, consumo, maquina);
  else if (key > node->key) node->right = insertNode(node->right, key, timestamp, consumo, maquina);
  return node;
}

void inOrder(Node* root, String& labels, String& values) {
  if (root != NULL) {
    inOrder(root->left, labels, values);
    labels += "\"" + root->timestamp + "\",";
    values += String(root->consumo) + ",";
    inOrder(root->right, labels, values);
  }
}

// ===== Pega último valor da árvore (nó mais à direita) =====
float getLastValue(Node* root) {
  if (root == NULL) return 0.0;
  Node* atual = root;
  while (atual->right != NULL) {
    atual = atual->right;
  }
  return atual->consumo;
}

// ===== Gerar timestamp com horário real via NTP =====
String gerarTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00:00"; // fallback
  }
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}

// ===== Página HTML =====
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="UTF-8">
    <title>Monitoramento de Consumo Elétrico</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css" rel="stylesheet">
    <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css" rel="stylesheet">
    <style>
      :root {
        --primary-color: #00d4ff;
        --secondary-color: #ff6b35;
        --background-dark: #0a0a0a;
        --card-dark: #1a1a1a;
        --text-light: #ffffff;
        --text-muted: #b0b0b0;
        --success-color: #00ff88;
        --warning-color: #ffaa00;
        --danger-color: #ff4757;
      }
      
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
      }
      
      body {
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        background: linear-gradient(135deg, var(--background-dark) 0%, #1a1a2e 50%, #16213e 100%);
        color: var(--text-light);
        min-height: 100vh;
        overflow-x: hidden;
      }
      
      .container {
        position: relative;
        z-index: 1;
      }
      
      .header {
        background: rgba(26, 26, 26, 0.9);
        backdrop-filter: blur(10px);
        border-radius: 20px;
        padding: 2rem;
        margin: 2rem 0;
        box-shadow: 0 8px 32px rgba(0, 212, 255, 0.1);
        border: 1px solid rgba(0, 212, 255, 0.2);
      }
      
      .header h1 {
        font-size: 2.5rem;
        font-weight: 700;
        background: linear-gradient(45deg, var(--primary-color), var(--secondary-color));
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        background-clip: text;
        text-align: center;
        margin-bottom: 0.5rem;
      }
      
      .header p {
        text-align: center;
        color: var(--text-muted);
        font-size: 1.1rem;
      }
      
      #alerta {
        font-weight: bold;
        font-size: 1.3rem;
        margin: 1.5rem 0;
        text-align: center;
        padding: 1rem;
        border-radius: 15px;
        background: rgba(255, 71, 87, 0.1);
        border: 2px solid var(--danger-color);
        animation: pulse 2s infinite;
      }
      
      @keyframes pulse {
        0% { transform: scale(1); }
        50% { transform: scale(1.05); }
        100% { transform: scale(1); }
      }
      
      .card {
        background: rgba(26, 26, 26, 0.8);
        backdrop-filter: blur(15px);
        border-radius: 20px;
        padding: 2rem;
        box-shadow: 0 10px 40px rgba(0, 0, 0, 0.3);
        border: 1px solid rgba(255, 255, 255, 0.1);
        transition: all 0.3s ease;
        position: relative;
        overflow: hidden;
      }
      
      .card::before {
        content: '';
        position: absolute;
        top: 0;
        left: 0;
        right: 0;
        height: 3px;
        background: linear-gradient(90deg, var(--primary-color), var(--secondary-color));
      }
      
      .card:hover {
        transform: translateY(-5px);
        box-shadow: 0 15px 50px rgba(0, 212, 255, 0.2);
      }
      
      .card.alerta {
        animation: alertaPiscar 1s infinite;
        border-color: var(--danger-color);
        box-shadow: 0 0 30px rgba(255, 71, 87, 0.5);
      }
      
      @keyframes alertaPiscar {
        0% { background-color: rgba(26, 26, 26, 0.8); }
        50% { background-color: rgba(255, 71, 87, 0.1); }
        100% { background-color: rgba(26, 26, 26, 0.8); }
      }
      
      .card h2 {
        color: var(--primary-color);
        font-size: 1.8rem;
        font-weight: 600;
        margin-bottom: 1.5rem;
        text-align: center;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 0.5rem;
      }
      
      .card h2 i {
        font-size: 1.5rem;
      }
      
      canvas {
        max-width: 100%;
        border-radius: 10px;
        background: rgba(0, 0, 0, 0.2);
      }
      
      .control-panel {
        background: rgba(26, 26, 26, 0.9);
        backdrop-filter: blur(15px);
        border-radius: 20px;
        padding: 2rem;
        margin-top: 2rem;
        box-shadow: 0 10px 40px rgba(0, 0, 0, 0.3);
        border: 1px solid rgba(255, 255, 255, 0.1);
      }
      
      .control-panel h3 {
        color: var(--secondary-color);
        font-size: 1.5rem;
        font-weight: 600;
        margin-bottom: 1.5rem;
        text-align: center;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 0.5rem;
      }
      
      .form-control {
        background: rgba(0, 0, 0, 0.3);
        border: 2px solid rgba(255, 255, 255, 0.1);
        border-radius: 10px;
        color: var(--text-light);
        padding: 0.75rem 1rem;
        font-size: 1rem;
        transition: all 0.3s ease;
      }
      
      .form-control:focus {
        background: rgba(0, 0, 0, 0.5);
        border-color: var(--primary-color);
        box-shadow: 0 0 20px rgba(0, 212, 255, 0.3);
        color: var(--text-light);
      }
      
      .form-control::placeholder {
        color: var(--text-muted);
      }
      
      .btn-primary {
        background: linear-gradient(45deg, var(--primary-color), #0099cc);
        border: none;
        border-radius: 10px;
        padding: 0.75rem 2rem;
        font-weight: 600;
        font-size: 1rem;
        transition: all 0.3s ease;
        box-shadow: 0 5px 15px rgba(0, 212, 255, 0.3);
      }
      
      .btn-primary:hover {
        background: linear-gradient(45deg, #0099cc, var(--primary-color));
        transform: translateY(-2px);
        box-shadow: 0 8px 25px rgba(0, 212, 255, 0.4);
      }
      
      .btn-primary:active {
        transform: translateY(0);
      }
      
      #statusLimite {
        color: var(--success-color);
        font-weight: 600;
        font-size: 1.1rem;
        text-align: center;
        margin-top: 1rem;
        padding: 0.5rem;
        background: rgba(0, 255, 136, 0.1);
        border-radius: 10px;
        border: 1px solid rgba(0, 255, 136, 0.3);
      }
      
      .d-flex {
        gap: 1rem;
      }
      
      @media (max-width: 768px) {
        .header h1 {
          font-size: 2rem;
        }
        
        .card {
          padding: 1.5rem;
        }
        
        .card h2 {
          font-size: 1.5rem;
        }
        
        .d-flex {
          flex-direction: column;
          align-items: center;
        }
        
        .btn-primary {
          width: 100%;
          max-width: 200px;
        }
      }
      
      .loading {
        display: inline-block;
        width: 20px;
        height: 20px;
        border: 3px solid rgba(255, 255, 255, 0.3);
        border-radius: 50%;
        border-top-color: var(--primary-color);
        animation: spin 1s ease-in-out infinite;
      }
      
      @keyframes spin {
        to { transform: rotate(360deg); }
      }
    </style>
  </head>
  <body>
    <div class="container mt-4">
      <div class="header">
        <h1><i class="fas fa-bolt"></i> Monitoramento Elétrico</h1>
        <p>Sistema de monitoramento em tempo real de consumo elétrico</p>
      </div>
      
      <div id="alerta"></div>

      <!-- Dashboard principal -->
      <div class="row justify-content-center">
        <div class="col-12 col-md-10 col-lg-8 mb-4">
          <div id="card" class="card">
            <h2><i class="fas fa-chart-line"></i> Consumo Elétrico</h2>
            <canvas id="chart"></canvas>
          </div>
        </div>
      </div>

      <!-- Painel de controle -->
      <div class="control-panel text-center">
        <h3><i class="fas fa-cog"></i> Configurações</h3>
        <div class="d-flex justify-content-center flex-wrap mt-2">
          <input type="number" id="limite" class="form-control me-2 mb-2" placeholder="Ex: 150" style="max-width: 200px;">
          <button class="btn btn-primary mb-2" onclick="definirLimite()">
            <i class="fas fa-paper-plane"></i> Definir Limite
          </button>
        </div>
        <p id="statusLimite"></p>
      </div>
    </div>

    <script>
      let chart;
      let limite = null;

      async function carregarDados() {
        try {
          const resp = await fetch("/data");
          const dados = await resp.json();
          atualizarGrafico(chart, dados.labels, dados.values);

          if (limite !== null) {
            const ultimo = dados.values.length ? dados.values[dados.values.length-1] : 0;

            document.getElementById("card").classList.toggle("alerta", ultimo > limite);

            document.getElementById("alerta").innerText = ultimo > limite
              ? "⚠️ Consumo acima do limite!" : "";
          } else {
            document.getElementById("card").classList.remove("alerta");
            document.getElementById("alerta").innerText = "";
          }
        } catch(e) { 
          console.error("Erro ao carregar dados:", e); 
        }
      }

      function criarGrafico(canvasId, titulo, cor) {
        return new Chart(document.getElementById(canvasId), {
          type: "line",
          data: {
            labels: [],
            datasets: [{ 
              label: titulo, 
              data: [], 
              borderColor: cor, 
              backgroundColor: cor.replace("1)","0.2)"), 
              fill: true, 
              tension: 0.4, 
              borderWidth: 3, 
              pointRadius: 4,
              pointBackgroundColor: cor,
              pointBorderColor: '#ffffff',
              pointBorderWidth: 2
            }]
          },
          options: { 
            responsive: true, 
            animation: {
              duration: 1000,
              easing: 'easeInOutQuart'
            },
            plugins: {
              legend: {
                labels: {
                  color: '#ffffff',
                  font: {
                    size: 14,
                    weight: 'bold'
                  }
                }
              }
            },
            scales: { 
              x: { 
                title: { 
                  display: true, 
                  text: "Tempo",
                  color: '#00d4ff',
                  font: {
                    size: 14,
                    weight: 'bold'
                  }
                },
                ticks: {
                  color: '#b0b0b0'
                },
                grid: {
                  color: 'rgba(255, 255, 255, 0.1)'
                }
              }, 
              y: { 
                title: { 
                  display: true, 
                  text: "Consumo (W)",
                  color: '#00d4ff',
                  font: {
                    size: 14,
                    weight: 'bold'
                  }
                }, 
                beginAtZero: true,
                ticks: {
                  color: '#b0b0b0'
                },
                grid: {
                  color: 'rgba(255, 255, 255, 0.1)'
                }
              } 
            } 
          }
        });
      }

      function atualizarGrafico(chart, labels, values) {
        chart.data.labels = labels;
        chart.data.datasets[0].data = values;
        chart.update('none');
      }

      async function definirLimite() {
        const valor = document.getElementById("limite").value;
        if(!valor) return;
        
        const btn = event.target;
        const originalText = btn.innerHTML;
        btn.innerHTML = '<span class="loading"></span> Definindo...';
        btn.disabled = true;
        
        limite = parseFloat(valor);
        await fetch("/setLimit?valor="+valor);
        document.getElementById("statusLimite").innerText = "✅ Limite definido: "+valor+"W";
        
        btn.innerHTML = originalText;
        btn.disabled = false;
      }

      chart = criarGrafico("chart","Consumo Elétrico","rgba(0, 212, 255, 1)");

      setInterval(carregarDados, 1000);
      carregarDados();
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  String labels = "", values = "";

  inOrder(root, labels, values);

  if (labels.endsWith(",")) labels.remove(labels.length()-1);
  if (values.endsWith(",")) values.remove(values.length()-1);

  String json = "{";
  json += "\"labels\":[" + labels + "],";
  json += "\"values\":[" + values + "]";
  json += "}";

  server.send(200, "application/json", json);
}

// ===== Rota para definir limite =====
void handleSetLimit() {
  if (server.hasArg("valor")) {
    limiteConsumo = server.arg("valor").toFloat();
    Serial.printf("Novo limite definido: %.2fW\n", limiteConsumo);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Parâmetro 'valor' não encontrado");
  }
}

// ===== Controle do LED =====
unsigned long lastBlinkTime = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // Configuração do sensor de corrente
  SCT013.current(pinSCT, 6.0606);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.println(WiFi.localIP());

  // Configura NTP (UTC-3 = Brasília)
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  // Rotas do servidor
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setLimit", handleSetLimit);
  server.begin();
  Serial.println("Servidor HTTP iniciado!");
}

void loop() {
  server.handleClient();

  // Medição contínua de corrente
  double Irms = SCT013.calcIrms(1480);   // Calcula o valor da Corrente
  float potencia = Irms * tensao;        // Calcula o valor da Potencia Instantanea

  unsigned long newKey = millis() / 1000;
  String ts = gerarTimestamp();

  // Inserir medição atual na árvore
  root = insertNode(root, newKey, ts, potencia, 1);

  Serial.printf("[Medição] %s -> Corrente: %.2fA, Potência: %.2fW\n", ts.c_str(), Irms, potencia);

  // Verifica último valor da árvore e mantém o alerta se estiver acima do limite
  float ultimo = getLastValue(root);
  alertaAtivo = (ultimo > limiteConsumo);

  // Piscar LED rápido (100ms) enquanto alerta estiver ativo
  if (alertaAtivo) {
    if (millis() - lastBlinkTime >= 100) {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}