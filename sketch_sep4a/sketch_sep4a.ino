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
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  </head>
  <body style="font-family: Arial; text-align: center;">
    <h2>Máquina 1</h2>
    <canvas id="chart1"></canvas>
    <h2>Máquina 2</h2>
    <canvas id="chart2"></canvas>

    <h3>Definir limite de consumo:</h3>
    <input type="number" id="limite" placeholder="Ex: 150">
    <button onclick="definirLimite()">Enviar</button>
    <p id="statusLimite"></p>

   <script>
    let chart1, chart2;

    async function carregarDados() {
      try {
        const resp = await fetch("/data");
        const dados = await resp.json();

        atualizarGrafico(chart1, dados.labels1, dados.values1);
        atualizarGrafico(chart2, dados.labels2, dados.values2);
      } catch (e) {
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
            backgroundColor: cor.replace("1)", "0.2)"),
            fill: true,
            tension: 0.3,
            borderWidth: 2,
            pointRadius: 3
          }]
        },
        options: {
          responsive: true,
          animation: false,
          scales: {
            x: { title: { display: true, text: "Tempo" } },
            y: { title: { display: true, text: "Consumo (W)" }, beginAtZero: true }
          }
        }
      });
    }

    function atualizarGrafico(chart, labels, values) {
      chart.data.labels = labels;
      chart.data.datasets[0].data = values;
      chart.update();
    }

    async function definirLimite() {
      const valor = document.getElementById("limite").value;
      if (!valor) return;
      await fetch("/setLimit?valor=" + valor);
      document.getElementById("statusLimite").innerText = "Limite definido: " + valor + "W";
    }

    chart1 = criarGrafico("chart1", "Consumo Máquina 1", "rgba(54, 162, 235, 1)");
    chart2 = criarGrafico("chart2", "Consumo Máquina 2", "rgba(255, 99, 132, 1)");

    setInterval(carregarDados, 1000);
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
