#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// ===== CONFIG WIFI =====
const char* ssid = "M54teste";
const char* password = "123456789";

// ===== Servidor =====
WebServer server(80);

// ===== LED =====
const int LED_PIN = 2;  // LED embutido do ESP32 (troque se necessário)
bool alertaAtivo = false;

// ===== Limite de consumo (valor inicial = 150W) =====
float limiteConsumo = 9999;

// ===== Estrutura da árvore =====
struct Node {
  unsigned long key;
  String timestamp;
  float consumo;
  int maquina;   // número da máquina
  Node* left;
  Node* right;
};

Node* root1 = NULL; // Árvore da máquina 1
Node* root2 = NULL; // Árvore da máquina 2

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
    <title>Consumo de Máquinas</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
      body { font-family: Arial; background-color: #f5f5f5; margin:0; padding:0; }
      #alerta { font-weight:bold; color:#ff0000; font-size:20px; margin-bottom:20px; text-align:center; }
      .alerta { animation: piscar 1s infinite; }
      @keyframes piscar { 0% { background-color:#fff;} 50% {background-color:#ffcccc;} 100%{background-color:#fff;} }
      canvas { max-width: 100%; }
      input[type="number"] { max-width:150px; }
    </style>
  </head>
  <body>
    <div class="container mt-4">
      <h1 class="mb-4 text-center">Monitoramento de Consumo de Máquinas</h1>
      <div id="alerta"></div>

      <!-- Row dos dashboards -->
      <div class="row justify-content-center">
        <div class="col-12 col-md-6 mb-3">
          <div id="card1" class="card p-3">
            <h2 class="text-center">Máquina 1</h2>
            <canvas id="chart1"></canvas>
          </div>
        </div>
        <div class="col-12 col-md-6 mb-3">
          <div id="card2" class="card p-3">
            <h2 class="text-center">Máquina 2</h2>
            <canvas id="chart2"></canvas>
          </div>
        </div>
      </div>

      <!-- Card do limite de consumo -->
      <div class="card p-3 mt-3 text-center">
        <h3>Definir limite de consumo (W)</h3>
        <div class="d-flex justify-content-center flex-wrap mt-2">
          <input type="number" id="limite" class="form-control me-2 mb-2" placeholder="Ex: 150">
          <button class="btn btn-primary mb-2" onclick="definirLimite()">Enviar</button>
        </div>
        <p id="statusLimite" class="mt-2"></p>
      </div>
    </div>

    <script>
      let chart1, chart2;
      let limite = null;

      async function carregarDados() {
        try {
          const resp = await fetch("/data");
          const dados = await resp.json();
          atualizarGrafico(chart1, dados.labels1, dados.values1);
          atualizarGrafico(chart2, dados.labels2, dados.values2);

          if (limite !== null) {
            const ultimo1 = dados.values1.length ? dados.values1[dados.values1.length-1] : 0;
            const ultimo2 = dados.values2.length ? dados.values2[dados.values2.length-1] : 0;

            document.getElementById("card1").classList.toggle("alerta", ultimo1 > limite);
            document.getElementById("card2").classList.toggle("alerta", ultimo2 > limite);

            document.getElementById("alerta").innerText = (ultimo1 > limite || ultimo2 > limite)
              ? "⚠️ Consumo acima do limite!" : "";
          } else {
            document.getElementById("card1").classList.remove("alerta");
            document.getElementById("card2").classList.remove("alerta");
            document.getElementById("alerta").innerText = "";
          }
        } catch(e) { console.error("Erro ao carregar dados:", e); }
      }

      function criarGrafico(canvasId, titulo, cor) {
        return new Chart(document.getElementById(canvasId), {
          type: "line",
          data: {
            labels: [],
            datasets: [{ label: titulo, data: [], borderColor: cor, backgroundColor: cor.replace("1)","0.2)"), fill:true, tension:0.3, borderWidth:2, pointRadius:3 }]
          },
          options: { responsive:true, animation:false, scales: { x: { title:{ display:true, text:"Tempo" } }, y: { title:{ display:true, text:"Consumo (W)" }, beginAtZero:true } } }
        });
      }

      function atualizarGrafico(chart, labels, values) {
        chart.data.labels = labels;
        chart.data.datasets[0].data = values;
        chart.update();
      }

      async function definirLimite() {
        const valor = document.getElementById("limite").value;
        if(!valor) return;
        limite = parseFloat(valor);
        await fetch("/setLimit?valor="+valor);
        document.getElementById("statusLimite").innerText = "Limite definido: "+valor+"W";
      }

      chart1 = criarGrafico("chart1","Consumo Máquina 1","rgba(54, 162, 235, 1)");
      chart2 = criarGrafico("chart2","Consumo Máquina 2","rgba(255, 99, 132, 1)");

      setInterval(carregarDados,1000);
      carregarDados();
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}





void handleData() {
  String labels1 = "", values1 = "";
  String labels2 = "", values2 = "";

  inOrder(root1, labels1, values1);
  inOrder(root2, labels2, values2);

  if (labels1.endsWith(",")) labels1.remove(labels1.length()-1);
  if (values1.endsWith(",")) values1.remove(values1.length()-1);
  if (labels2.endsWith(",")) labels2.remove(labels2.length()-1);
  if (values2.endsWith(",")) values2.remove(values2.length()-1);

  String json = "{";
  json += "\"labels1\":[" + labels1 + "],";
  json += "\"values1\":[" + values1 + "],";
  json += "\"labels2\":[" + labels2 + "],";
  json += "\"values2\":[" + values2 + "]";
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

// ===== Inserção automática e controle do LED =====
unsigned long lastInsertTime = 0;
unsigned long lastBlinkTime = 0;
bool ledState = false;

void loop() {
  server.handleClient();

  // Inserir novos valores a cada 10s
  if (millis() - lastInsertTime >= 10000) {
    lastInsertTime = millis();

    unsigned long newKey = millis() / 1000;
    String ts = gerarTimestamp();

    float consumo1 = random(90, 200);
    float consumo2 = random(100, 220);

    root1 = insertNode(root1, newKey, ts, consumo1, 1);
    root2 = insertNode(root2, newKey, ts, consumo2, 2);

    Serial.printf("[Maquina 1] %s -> %.2fW\n", ts.c_str(), consumo1);
    Serial.printf("[Maquina 2] %s -> %.2fW\n", ts.c_str(), consumo2);
  }

  // Verifica último valor das duas máquinas e mantem o alerta enquanto algum estiver acima
  float ultimo1 = getLastValue(root1);
  float ultimo2 = getLastValue(root2);
  alertaAtivo = (ultimo1 > limiteConsumo || ultimo2 > limiteConsumo);

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

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

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

  // Rotas do servidor - **ATENÇÃO**: todas com ';' ao final
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setLimit", handleSetLimit);
  server.begin();
  Serial.println("Servidor HTTP iniciado!");
}
