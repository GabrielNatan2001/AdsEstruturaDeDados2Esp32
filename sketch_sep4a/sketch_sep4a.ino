#include <WiFi.h>
#include <WebServer.h>

// ===== CONFIG WIFI =====
const char* ssid = "M54teste";
const char* password = "123456789";

// ===== Servidor =====
WebServer server(80);

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

// ===== Gerador de timestamp fake =====
int counter = 0;
String gerarTimestamp() {
  int h = (counter / 6) % 24;
  int m = (counter * 10) % 60;
  char buffer[20];
  sprintf(buffer, "%02d:%02d", h, m);
  return String(buffer);
}

// ===== Rotas =====
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
            backgroundColor: cor.replace("1)", "0.2)"), // mesma cor mais clara no fundo
            fill: true,
            tension: 0.3,
            borderWidth: 2,
            pointRadius: 3
          }]
        },
        options: {
          responsive: true,
          animation: false, // sem animação para atualizar rápido
          scales: {
            x: { 
              title: { display: true, text: "Tempo" } 
            },
            y: { 
              title: { display: true, text: "Consumo (W)" },
              beginAtZero: true 
            }
          }
        }
      });
    }

    function atualizarGrafico(chart, labels, values) {
      chart.data.labels = labels;
      chart.data.datasets[0].data = values;
      chart.update();
    }

    // Cria os gráficos vazios com cores diferentes
    chart1 = criarGrafico("chart1", "Consumo Máquina 1", "rgba(54, 162, 235, 1)");   // Azul
    chart2 = criarGrafico("chart2", "Consumo Máquina 2", "rgba(255, 99, 132, 1)");  // Vermelho

    // Atualiza automaticamente a cada 5 segundos
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

// ===== Inserção automática =====
unsigned long lastInsertTime = 0;
unsigned long baseKey = 1694300000;

void loop() {
  server.handleClient();

  if (millis() - lastInsertTime >= 10000) {
    lastInsertTime = millis();
    counter++;

    unsigned long newKey = baseKey + counter * 600;
    String ts = gerarTimestamp();

    float consumo1 = random(90, 160);
    float consumo2 = random(100, 180);

    root1 = insertNode(root1, newKey, ts, consumo1, 1);
    root2 = insertNode(root2, newKey, ts, consumo2, 2);

    Serial.printf("[Maquina 1] %s -> %.2fW\n", ts.c_str(), consumo1);
    Serial.printf("[Maquina 2] %s -> %.2fW\n", ts.c_str(), consumo2);
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Servidor HTTP iniciado!");
}
